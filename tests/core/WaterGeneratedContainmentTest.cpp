// WaterCore.md 51.12 - the RiverCarve lake wall (51.4c item 2): the generated record holds water the terrain does not hold.
//
// Measures, on the bench world's OWN recipe (tests/core/golden/rivercarve_recipe.json, copied from
// WaterBench_RiverCarve/worlds/default.db world_meta "recipe"), every place a generated water span stands next to a column
// whose ground is below that water and which holds no water at the same level - a free-standing wall. Each wall face is
// classified by why the flood (floodBodiesOverGrid) did not continue into the dry column:
//   river    - an order >= 3 channel column (holds its river span, never a lake's level)
//   drains   - the hydraulic rule: the coarse depression-filled height there is below the level (filled < level)
//   unreached- neither: the step bound (kWaterExtentSteps) or a seed beyond the sampled margin
// and a wet neighbour lower by more than 0.25 m (two levels meeting with no ground between) as a step.
#include <gtest/gtest.h>
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"
#include "core/HydrologyMap.h"
#include "core/WaterOccupancy.h"
#include "core/FlowField.h"
#include <chrono>
#include <string>
#include <filesystem>
#include <thread>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cmath>
#include <map>
#include <algorithm>

using namespace Phyxel;

namespace {

// drains: WorldRecipe::carveDrainsBasins (51.12). The bench's own recipe predates it (no key -> false).
// floods: WorldRecipe::bakeFloodsTerrain (51.13), also absent from the bench's recipe.
bool loadRiverCarve(WorldGenerator& g, bool drains = false, bool floods = false, bool levels = false) {
    std::ifstream f("tests/core/golden/rivercarve_recipe.json");
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    WorldRecipe r = WorldRecipe::fromJson(ss.str());
    r.carveDrainsBasins = drains;
    r.bakeFloodsTerrain = floods;
    r.levelRivers = levels;   // 51.14
    g.applyRecipe(r);
    return g.hydrology() != nullptr;
}

struct Census {
    long wet = 0, wallRiver = 0, wallDrains = 0, wallUnreached = 0, steps = 0;
    float worstWall = 0.0f; int wx = 0, wz = 0; float wLevel = 0.0f; int wGround = 0; const char* wWhy = "";
    std::map<int, long> wetByLevel;   // rounded level -> wet columns
    // A wet column that is itself a carved river column holds a river span (bed + carve depth, D4) - its edges are the river
    // representation's, not a lake's: counted apart, never in the wall faces.
    long riverSpanEdges = 0; float worstRiverSpanEdge = 0.0f; int rex = 0, rez = 0;
    // 51.14: water over water where either side is a river column, the exposed face higher than 0.25 m.
    long riverSteps = 0; float worstRiverStep = 0.0f; int rsx = 0, rsz = 0;
    long riverColumns = 0, riverWet = 0;
    long bankOver[5] = {0, 0, 0, 0, 0};   // a river column's dry neighbour: its top over the water, < 1 | 1-2 | 2-4 | 4-8 | >= 8 m
    float worstBy[3] = {0, 0, 0}; int wbx[3] = {0, 0, 0}, wbz[3] = {0, 0, 0}, wbg[3] = {0, 0, 0}; float wbl[3] = {0, 0, 0};
};

void report(const char* name, int x0, int z0, int w, int d, const Census& c) {
    std::printf("[%s] region x %d..%d z %d..%d\n", name, x0, x0 + w, z0, z0 + d);
    std::printf("  wet %ld | wall faces: river %ld, drains %ld, unreached %ld | steps %ld\n", c.wet, c.wallRiver, c.wallDrains,
                c.wallUnreached, c.steps);
    std::printf("  river-span edges (not lake walls) %ld, worst %.1f m at (%d, %d)\n", c.riverSpanEdges, c.worstRiverSpanEdge, c.rex, c.rez);
    std::printf("  river steps (water over water) %ld, worst %.2f m at (%d, %d); river columns %ld, wet %ld\n", c.riverSteps,
                c.worstRiverStep, c.rsx, c.rsz, c.riverColumns, c.riverWet);
    std::printf("  river banks over the water: <1 m %ld | 1-2 %ld | 2-4 %ld | 4-8 %ld | >=8 %ld\n", c.bankOver[0], c.bankOver[1],
                c.bankOver[2], c.bankOver[3], c.bankOver[4]);
    static const char* kWhy[3] = {"river", "drains", "unreached"};
    for (int k = 0; k < 3; ++k)
        if (c.worstBy[k] > 0) std::printf("  worst %-9s %.1f m at (%d, %d): level %.2f, ground top %d\n", kWhy[k], c.worstBy[k], c.wbx[k], c.wbz[k], c.wbl[k], c.wbg[k]);
    for (const auto& [lvl, n] : c.wetByLevel) if (n > 200) std::printf("  level %d: %ld columns\n", lvl, n);
}

Census census(WorldGenerator& g, int x0, int z0, int w, int d) {
    std::vector<WaterSpan> spans; std::vector<uint8_t> has;
    g.waterSpansForBlock(x0, z0, w, d, spans, has);
    const HydrologyMap* h = g.hydrology();
    std::vector<int> surf(static_cast<size_t>(w) * d), order(static_cast<size_t>(w) * d);
    std::vector<float> drawn(static_cast<size_t>(w) * d);
    for (int z = 0; z < d; ++z) for (int x = 0; x < w; ++x) {
        const auto cs = g.sampleSurface(x0 + x, z0 + z);
        surf[static_cast<size_t>(z) * w + x] = cs.surfaceY; order[static_cast<size_t>(z) * w + x] = cs.riverOrder;
        drawn[static_cast<size_t>(z) * w + x] = g.drawnTop(cs);   // 51.14: a creek shelf's top is below surfaceY + 1
    }
    Census c;
    for (size_t i = 0; i < order.size(); ++i) if (order[i] >= 3) { ++c.riverColumns; if (has[i]) ++c.riverWet; }
    static const int kN[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int z = 0; z < d; ++z) for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(z) * w + x;
        if (!has[i]) continue;
        ++c.wet;
        const float T = spans[i].topY;
        c.wetByLevel[static_cast<int>(std::lround(T))]++;
        for (const auto& n : kN) {
            const int nx = x + n[0], nz = z + n[1];
            if (nx < 0 || nz < 0 || nx >= w || nz >= d) continue;
            const size_t j = static_cast<size_t>(nz) * w + nx;
            const float groundTop = drawn[j];
            if (has[j]) {
                if (spans[j].topY < T - 0.25f && order[i] < 3 && order[j] < 3 && groundTop < T - 0.25f) ++c.steps;
                const float face = T - std::max(spans[j].topY, groundTop);
                if ((order[i] >= 3 || order[j] >= 3) && face > 0.25f) {
                    ++c.riverSteps;
                    if (face > c.worstRiverStep) { c.worstRiverStep = face; c.rsx = x0 + nx; c.rsz = z0 + nz; }
                }
                continue;
            }
            if (groundTop >= T - kMinSpanDepth) {   // the ground holds it
                if (order[i] >= 3) {
                    const float b = groundTop - T;
                    c.bankOver[b < 1 ? 0 : b < 2 ? 1 : b < 4 ? 2 : b < 8 ? 3 : 4]++;
                }
                continue;
            }
            if (order[i] >= 3) {
                ++c.riverSpanEdges;
                if (T - groundTop > c.worstRiverSpanEdge) { c.worstRiverSpanEdge = T - groundTop; c.rex = x0 + nx; c.rez = z0 + nz; }
                continue;
            }
            const float wx = static_cast<float>(x0 + nx) + 0.5f, wz = static_cast<float>(z0 + nz) + 0.5f;
            const char* why; int k;
            if (order[j] >= 3) { why = "river"; ++c.wallRiver; k = 0; }
            else if (h->filledAt(wx, wz) < T - 1e-3f) { why = "drains"; ++c.wallDrains; k = 1; }
            else { why = "unreached"; ++c.wallUnreached; k = 2; }
            const float height = T - groundTop;
            if (height > c.worstBy[k]) { c.worstBy[k] = height; c.wbx[k] = x0 + nx; c.wbz[k] = z0 + nz; c.wbg[k] = surf[j] + 1; c.wbl[k] = T; }
            if (height > c.worstWall) { c.worstWall = height; c.wx = x0 + nx; c.wz = z0 + nz; c.wLevel = T; c.wGround = surf[j]; c.wWhy = why; }
        }
    }
    return c;
}

}  // namespace

// Measurement (no assertion on the defect yet): where the 51.4c lake is and why its walls stand.
// PHYXEL_DRAIN=1 measures with the 51.12 drain on (the bench recipe's own default is off).
TEST(WaterGeneratedContainmentTest, DISABLED_DiagRiverCarveLakeWall) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    const char* drainEnv = std::getenv("PHYXEL_DRAIN");
    const char* floodEnv = std::getenv("PHYXEL_FLOODS");
    ASSERT_TRUE(loadRiverCarve(g, drainEnv && drainEnv[0] == '1', floodEnv && floodEnv[0] == '1')) << "fixture tests/core/golden/rivercarve_recipe.json (run from the repo root)";
    const HydrologyMap* h = g.hydrology();
    // 1. the bake's cells at the 51.4c level (107.8) round the bench camera (x -1728, north of z -1146)
    int bx0 = 1 << 30, bz0 = 1 << 30, bx1 = -(1 << 30), bz1 = -(1 << 30), cells = 0;
    for (int cz = -24; cz <= 8; ++cz) for (int cx = -26; cx <= -4; ++cx) {
        const float wx = (cx + 0.5f) * 128.0f, wz = (cz + 0.5f) * 128.0f;
        const float L = h->waterLevelAt(wx, wz);
        if (L < 105.0f || L > 111.0f) continue;
        ++cells; std::printf("  bake cell (%d, %d) level %.2f filled %.2f\n", static_cast<int>(wx), static_cast<int>(wz), L, h->filledAt(wx, wz));
        bx0 = std::min(bx0, cx * 128); bz0 = std::min(bz0, cz * 128); bx1 = std::max(bx1, cx * 128 + 128); bz1 = std::max(bz1, cz * 128 + 128);
    }
    ASSERT_GT(cells, 0) << "no bake cell at 105-111 - the fixture does not reproduce 51.4c's lake";
    const int x0 = bx0 - 64, z0 = bz0 - 64, w = std::min(bx1 - bx0 + 128, 768), d = std::min(bz1 - bz0 + 128, 768);
    report("bake bbox corner", x0, z0, w, d, census(g, x0, z0, w, d));
    // 2. the 51.4c pose itself (skin_river.json: camera (-1728, 115, -1320), yaw 90 = looking +Z): 768 m north of it
    report("51.4c view", -2112, -1216, 768, 768, census(g, -2112, -1216, 768, 768));
}

// 51.12 - the acceptance test. A window round the 51.4c lake's worst walls (the diagnostic above: river 81.8 m at (-1738, -1084),
// drains 63.6 m at (-1665, -1214), unreached 76.8 m at (-1715, -874)). Red control: the bake as it was (the recipe's flag off) -
// the census must SEE the wall, or the green half proves nothing. Green: a carved channel drains the basin it crosses, so no
// wet column stands beside a carved river column whose ground is below its water, and no wall is taller than 3 m.
TEST(WaterGeneratedContainmentTest, ACarvedChannelDrainsTheBasinItCrosses) {
    const int x0 = -1920, z0 = -1216, w = 384, d = 384;
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
        ASSERT_TRUE(loadRiverCarve(g, false)) << "fixture tests/core/golden/rivercarve_recipe.json (run from the repo root)";
        const Census c = census(g, x0, z0, w, d);
        report("control: drain off", x0, z0, w, d, c);
        ASSERT_GT(c.wallRiver, 0) << "control: the census must see the lake standing beside the carved river";
        ASSERT_GT(c.worstWall, 50.0f);
    }
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true));
    const Census c = census(g, x0, z0, w, d);
    report("drain on", x0, z0, w, d, c);
    EXPECT_EQ(c.wallRiver, 0) << "a lake still stands beside a carved river column whose ground is below it";
    EXPECT_LT(c.worstWall, 3.0f) << "worst wall at (" << c.wx << ", " << c.wz << "): level " << c.wLevel << ", ground top "
                                 << c.wGround + 1 << " (" << c.wWhy << ")";
}

// 51.12 - chunking must not change the answer: with the drain on, a 128 x 128 window across the former lake's shore and the river
// resolves the same as a whole as in its 16 chunk-sized blocks.
TEST(WaterGeneratedContainmentTest, ADrainedBasinIsTheSameChunkedOrWhole) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));   // both bake flags on (51.12 + 51.13)
    const int x0 = -1792, z0 = -1120, n = 128;
    std::vector<WaterSpan> whole; std::vector<uint8_t> wholeHas;
    g.waterSpansForBlock(x0, z0, n, n, whole, wholeHas);
    long differ = 0, wet = 0;
    for (int bz = 0; bz < n; bz += 32) for (int bx = 0; bx < n; bx += 32) {
        std::vector<WaterSpan> part; std::vector<uint8_t> partHas;
        g.waterSpansForBlock(x0 + bx, z0 + bz, 32, 32, part, partHas);
        for (int z = 0; z < 32; ++z) for (int x = 0; x < 32; ++x) {
            const size_t i = static_cast<size_t>(bz + z) * n + (bx + x), j = static_cast<size_t>(z) * 32 + x;
            wet += wholeHas[i];
            if (wholeHas[i] != partHas[j] ||
                (partHas[j] && (whole[i].bottomY != part[j].bottomY || whole[i].topY != part[j].topY))) ++differ;
        }
    }
    std::printf("chunked vs whole: %ld wet of %d, %ld differ\n", wet, n * n, differ);
    EXPECT_GT(wet, 0) << "the window holds no water - it tests nothing";
    EXPECT_EQ(differ, 0);
}

// 51.12 - the control over the whole bake: the drain touches ONLY bodies a carved channel crosses, only ever lowers, and never
// changes the sea. Every cell whose level differs between the drain off and on must belong to an undrained body that holds an
// order >= 3 cell; every other cell is bit-identical. Also reports the bake's cost with and without the drain.
TEST(WaterGeneratedContainmentTest, TheDrainTouchesOnlyBasinsACarvedChannelCrosses) {
    using clk = std::chrono::steady_clock;
    WorldGenerator off(WorldGenerator::GenerationType::Mountains, 20260729u);
    WorldGenerator::clearHydroBakeCache();
    auto t0 = clk::now();
    ASSERT_TRUE(loadRiverCarve(off, false));
    auto t1 = clk::now();
    WorldGenerator on(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(on, true));
    auto t2 = clk::now();
    const HydrologyMap& a = *off.hydrology();
    const HydrologyMap& b = *on.hydrology();
    const FlowField& f = *off.riverNetwork();
    std::printf("bake (load + recipe, cache cleared): drain off %.2f s, drain on %.2f s; drained %d, unresolved %d\n",
                std::chrono::duration<double>(t1 - t0).count(), std::chrono::duration<double>(t2 - t1).count(),
                b.basinsDrained(), b.basinsUnresolved());
    ASSERT_EQ(a.levels().size(), b.levels().size());
    const int cx = a.cellsX(), cz = a.cellsZ(), n = cx * cz;
    // Label the drain-off bodies (the same rule as the drain) and whether a carved channel crosses each.
    std::vector<int> body(n, -1); std::vector<uint8_t> crossed;
    const auto& L = a.levels();
    auto inland = [&](int c) { return L[c] > HydrologyMap::NO_WATER * 0.5f && L[c] > a.seaLevel() + 1e-3f; };
    for (int s0 = 0; s0 < n; ++s0) {
        if (!inland(s0) || body[s0] >= 0) continue;
        const int id = static_cast<int>(crossed.size()); crossed.push_back(0);
        std::vector<int> st{s0}; body[s0] = id;
        while (!st.empty()) {
            const int c = st.back(); st.pop_back();
            const int x = c % cx, z = c / cx;
            for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {   // its own cells or the 8 round them
                const int qx = x + dx, qz = z + dz;
                if (qx >= 0 && qz >= 0 && qx < cx && qz < cz && f.orderOfCell(qz * cx + qx) >= 3) crossed[id] = 1;
            }
            const int nb[4][2] = {{x + 1, z}, {x - 1, z}, {x, z + 1}, {x, z - 1}};
            for (const auto& q : nb) {
                if (q[0] < 0 || q[1] < 0 || q[0] >= cx || q[1] >= cz) continue;
                const int k = q[1] * cx + q[0];
                if (body[k] < 0 && inland(k) && std::fabs(L[k] - L[c]) <= 1e-3f) { body[k] = id; st.push_back(k); }
            }
        }
    }
    long changed = 0, changedUncrossed = 0, raised = 0, seaChanged = 0, nowDry = 0;
    for (int c = 0; c < n; ++c) {
        const float la = a.levels()[c], lb = b.levels()[c];
        if (la == lb) continue;
        ++changed;
        if (body[c] < 0) { ++seaChanged; continue; }
        if (!crossed[body[c]]) ++changedUncrossed;
        if (lb > HydrologyMap::NO_WATER * 0.5f && lb > la) ++raised;
        if (lb <= HydrologyMap::NO_WATER * 0.5f) ++nowDry;
    }
    long crossedBodies = 0; for (auto v : crossed) crossedBodies += v;
    std::printf("bodies %zu (crossed %ld); cells changed %ld (now dry %ld), in uncrossed bodies %ld, raised %ld, sea/dry changed %ld\n",
                crossed.size(), crossedBodies, changed, nowDry, changedUncrossed, raised, seaChanged);
    EXPECT_GT(changed, 0) << "the drain changed nothing - the acceptance test would be vacuous";
    EXPECT_EQ(changedUncrossed, 0);
    EXPECT_EQ(raised, 0);
    EXPECT_EQ(seaChanged, 0);
}

// 51.12 diagnostic: the bake cells round a point - level drain off / on, the flow field's order and downstream.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagCellsRoundTheWorstWall) {
    WorldGenerator off(WorldGenerator::GenerationType::Mountains, 20260729u), on(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(off, false)); ASSERT_TRUE(loadRiverCarve(on, true));
    const HydrologyMap& a = *off.hydrology(); const HydrologyMap& b = *on.hydrology(); const FlowField& f = *off.riverNetwork();
    const int pci = static_cast<int>(std::floor((-1738.0f - a.originX()) / a.cellSize()));
    const int pcj = static_cast<int>(std::floor((-1084.0f - a.originZ()) / a.cellSize()));
    for (int j = pcj - 3; j <= pcj + 3; ++j) {
        for (int i = pci - 3; i <= pci + 3; ++i) {
            const int c = j * a.cellsX() + i, dn = f.downstreamOfCell(c);
            const float la = a.levels()[c], lb = b.levels()[c];
            std::printf("%s(%4d,%4d) %6.1f>%6.1f o%d d%+d,%+d  ", (i == pci && j == pcj) ? "*" : " ",
                        static_cast<int>(a.originX() + i * a.cellSize()), static_cast<int>(a.originZ() + j * a.cellSize()),
                        la < -1e29f ? -1.0f : la, lb < -1e29f ? -1.0f : lb, f.orderOfCell(c), dn % a.cellsX() - i, dn / a.cellsX() - j);
        }
        std::printf("\n");
    }
}

// 51.13 diagnostic: why does the bake's coarse rim stand where the generated terrain has none? Over the bake cells of the
// residual-wall regions (drain on), per cell: (a) the SAME POINT - the bake's height at the cell's corner minus the generated
// surface there (the formula: biome blend, valley shaping, carve); (b) THE CELL - the generated surface at that corner minus its
// minimum over the cell (sampled every `stride` m; one cell exhaustively to check the stride). Percentiles of each.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagBakeHeightAgainstTheTerrain) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true));
    const HydrologyMap& h = *g.hydrology();
    const float cs = h.cellSize();
    const int stride = 4;
    std::vector<float> pointDiff, cellDrop, lakePointDiff, lakeCellDrop;
    auto fine = [&](int x, int z) { return static_cast<float>(g.sampleSurface(x, z).surfaceY + 1); };
    for (int cz = -25; cz < -3; ++cz) for (int cx = -18; cx < -10; ++cx) {   // x -2304..-1408, z -3200..-384
        const int x0 = cx * static_cast<int>(cs), z0 = cz * static_cast<int>(cs);
        const float hb = g.bakeHeightAt(static_cast<float>(x0), static_cast<float>(z0));
        const float fc = fine(x0, z0);
        float fmin = 1e30f;
        for (int z = z0; z < z0 + static_cast<int>(cs); z += stride)
            for (int x = x0; x < x0 + static_cast<int>(cs); x += stride) fmin = std::min(fmin, fine(x, z));
        const bool lake = h.hasWater(x0 + 1.0f, z0 + 1.0f) && h.waterLevelAt(x0 + 1.0f, z0 + 1.0f) > h.seaLevel() + 1e-3f;
        pointDiff.push_back(hb - fc); cellDrop.push_back(fc - fmin);
        if (lake) { lakePointDiff.push_back(hb - fc); lakeCellDrop.push_back(fc - fmin); }
    }
    auto pct = [](std::vector<float> v, const char* name) {
        if (v.empty()) { std::printf("%s: none\n", name); return; }
        std::sort(v.begin(), v.end());
        auto at = [&](float q) { return v[static_cast<size_t>(q * (v.size() - 1))]; };
        std::printf("%-44s n %4zu | min %7.1f p10 %7.1f p50 %7.1f p90 %7.1f max %7.1f\n", name, v.size(), v.front(), at(0.1f), at(0.5f),
                    at(0.9f), v.back());
    };
    pct(pointDiff, "(a) bake height - terrain, same point");
    pct(cellDrop, "(b) terrain at corner - its cell minimum");
    pct(lakePointDiff, "(a) lake cells only");
    pct(lakeCellDrop, "(b) lake cells only");
    // the stride check: one cell exhaustively
    const int x0 = -1664, z0 = -2816;
    float m1 = 1e30f, m4 = 1e30f;
    for (int z = z0; z < z0 + 128; ++z) for (int x = x0; x < x0 + 128; ++x) {
        const float f = fine(x, z); m1 = std::min(m1, f); if ((x - x0) % stride == 0 && (z - z0) % stride == 0) m4 = std::min(m4, f);
    }
    std::printf("stride check cell (%d, %d): min every 1 m %.1f, every %d m %.1f\n", x0, z0, m1, stride, m4);
}

// 51.13 step 2 - THE ACCEPTANCE TEST: no wall of water stands anywhere (the owner, 2026-11-13: "obviously walls of floating water
// cant exist"). Both RiverCarve regions, both bake flags on: zero lake-wall faces - a wet non-river column beside a dry column whose
// ground is below its water, whatever the reason the flood stopped. River spans' own steps are reported apart (the next item).
// Control: the drain alone (51.12) - the census must still see its walls in both regions.
TEST(WaterGeneratedContainmentTest, NoWallOfWaterStandsAnywhere) {
    struct Region { const char* name; int x0, z0; } regions[2] = {{"north", -2112, -3136}, {"51.4c view", -2112, -1216}};
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
        ASSERT_TRUE(loadRiverCarve(g, true, false));
        for (const auto& r : regions) {
            const Census c = census(g, r.x0, r.z0, 768, 768);
            report(r.name, r.x0, r.z0, 768, 768, c);
            ASSERT_GT(c.wallRiver + c.wallDrains + c.wallUnreached, 300) << "control: 51.12's walls must reproduce in " << r.name;
        }
    }
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    // 51.14: every new-world flag. The census reads the DRAWN top since 51.14 (a creek shelf is below surfaceY + 1), which found
    // 11 faces at creek mouths in the view that 51.13's guarantee missed; 51.14 fixes them under its own flag.
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    for (const auto& r : regions) {
        const Census c = census(g, r.x0, r.z0, 768, 768);
        report(r.name, r.x0, r.z0, 768, 768, c);
        EXPECT_EQ(c.wallRiver + c.wallDrains + c.wallUnreached, 0)
            << r.name << ": worst wall at (" << c.wx << ", " << c.wz << "): level " << c.wLevel << ", ground top " << c.wGround + 1
            << " (" << c.wWhy << ")";
        long lifted = 0; int maxLift = 0;
        for (int z = r.z0; z < r.z0 + 768; z += 2) for (int x = r.x0; x < r.x0 + 768; x += 2) {   // every 4th column
            const int l = g.sampleSurface(x, z).shoreLift;
            if (l > 0) { ++lifted; maxLift = std::max(maxLift, l); }
        }
        std::printf("  %s: shore guarantee lifted %ld of %d sampled columns (every 2nd x and z), largest lift %d m\n", r.name,
                    lifted, 384 * 384, maxLift);
    }
}

// 51.13 - the off flag changes nothing: with bakeFloodsTerrain off the bake's levels and filled are bit-identical to a generator that
// never heard of it (the default constructor path, then the same recipe).
TEST(WaterGeneratedContainmentTest, TheFloodsFlagOffLeavesTheBakeAsItWas) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 20260729u), b(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(a, true, false));
    WorldGenerator::clearHydroBakeCache();
    ASSERT_TRUE(loadRiverCarve(b, true, true));
    WorldGenerator::clearHydroBakeCache();
    WorldGenerator c(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(c, true, false));   // re-baked from scratch after a floods-on bake: no state leaks
    EXPECT_EQ(a.hydrology()->levels(), c.hydrology()->levels());
    EXPECT_NE(a.hydrology()->levels(), b.hydrology()->levels()) << "the flag must change the bake on this world";
}

// 51.13 step 2 measurement: what flooding each bake cell's LOWER BOUND would cost. (1) the cost of one generated-column sample;
// (2) over 40 cells (exhaustive 1 m minimum each), how far a minimum sampled every s m misses the true one (s = 4, 8, 16, 32).
TEST(WaterGeneratedContainmentTest, DISABLED_DiagCellMinimumCost) {
    using clk = std::chrono::steady_clock;
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, false));
    volatile int sink = 0;
    const int n = 50000;
    auto t0 = clk::now();
    for (int i = 0; i < n; ++i) sink += g.sampleSurface(-6000 + (i * 7919) % 12000, -6000 + (i * 104729) % 12000).surfaceY;
    const double us = std::chrono::duration<double, std::micro>(clk::now() - t0).count() / n;
    std::printf("sampleColumn: %.1f us a column (%d scattered columns)\n", us, n);
    for (int s : {32, 16, 8, 4})
        std::printf("  a full bake (65,536 cells) at every %2d m: %8.0f samples, ~%.0f s\n", s, 65536.0 * (128 / s) * (128 / s),
                    65536.0 * (128 / s) * (128 / s) * us * 1e-6);
    const int strides[4] = {4, 8, 16, 32};
    std::vector<float> miss[4];
    for (int k = 0; k < 40; ++k) {
        const int x0 = -2304 + (k % 8) * 128, z0 = -3200 + (k / 8) * 512;
        float exact = 1e30f, sm[4] = {1e30f, 1e30f, 1e30f, 1e30f};
        for (int z = 0; z < 128; ++z) for (int x = 0; x < 128; ++x) {
            const float f = static_cast<float>(g.sampleSurface(x0 + x, z0 + z).surfaceY + 1);
            exact = std::min(exact, f);
            for (int q = 0; q < 4; ++q) if (x % strides[q] == 0 && z % strides[q] == 0) sm[q] = std::min(sm[q], f);
        }
        for (int q = 0; q < 4; ++q) miss[q].push_back(sm[q] - exact);
    }
    for (int q = 0; q < 4; ++q) {
        std::sort(miss[q].begin(), miss[q].end());
        std::printf("  every %2d m misses the true minimum by: p50 %.0f, p90 %.0f, max %.0f m (40 cells)\n", strides[q],
                    miss[q][20], miss[q][36], miss[q].back());
    }
}

// 51.13 - chunking must not change the SHORE GUARANTEE: find a lifted column in the 51.4c view (both flags on), then a 128 x 128
// window round it (chunk-aligned) must resolve the same as a whole as in its 16 chunk-sized blocks, and the chunk column cache must
// return the lifted surface the direct sample does.
TEST(WaterGeneratedContainmentTest, TheShoreGuaranteeIsTheSameChunkedOrWhole) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));
    int lx = INT_MIN, lz = INT_MIN;
    for (int z = -1216; z < -448 && lx == INT_MIN; ++z)
        for (int x = -2112; x < -1344; ++x)
            if (g.sampleSurface(x, z).shoreLift > 0) { lx = x; lz = z; break; }
    ASSERT_NE(lx, INT_MIN) << "no lifted column in the 51.4c view - the test would be vacuous";
    auto fd = [](int a) { return a >= 0 ? a / 32 : -((-a + 31) / 32); };
    const int x0 = (fd(lx) - 1) * 32, z0 = (fd(lz) - 1) * 32, n = 128;
    std::vector<WaterSpan> whole; std::vector<uint8_t> wholeHas;
    g.waterSpansForBlock(x0, z0, n, n, whole, wholeHas);
    long differ = 0, wet = 0, lifted = 0, cacheDiffer = 0;
    for (int bz = 0; bz < n; bz += 32) for (int bx = 0; bx < n; bx += 32) {
        std::vector<WaterSpan> part; std::vector<uint8_t> partHas;
        g.waterSpansForBlock(x0 + bx, z0 + bz, 32, 32, part, partHas);
        for (int z = 0; z < 32; ++z) for (int x = 0; x < 32; ++x) {
            const size_t i = static_cast<size_t>(bz + z) * n + (bx + x), j = static_cast<size_t>(z) * 32 + x;
            wet += wholeHas[i];
            if (wholeHas[i] != partHas[j] ||
                (partHas[j] && (whole[i].bottomY != part[j].bottomY || whole[i].topY != part[j].topY))) ++differ;
        }
    }
    for (int z = 0; z < n; ++z) for (int x = 0; x < n; ++x) {
        const auto direct = g.sampleSurface(x0 + x, z0 + z);
        lifted += direct.shoreLift > 0;
        const auto cols = g.columnsForChunkForTest(glm::ivec2(fd(x0 + x), fd(z0 + z)));
        const auto& c = (*cols)[static_cast<size_t>((x0 + x) - fd(x0 + x) * 32) * 32 + ((z0 + z) - fd(z0 + z) * 32)];
        if (c.surfaceY != direct.surfaceY || c.shoreLift != direct.shoreLift) ++cacheDiffer;
    }
    std::printf("lifted column (%d, %d); window x %d z %d: %ld wet, %ld lifted, %ld differ chunked vs whole, %ld cache vs direct\n",
                lx, lz, x0, z0, wet, lifted, differ, cacheDiffer);
    EXPECT_GT(lifted, 0);
    EXPECT_EQ(differ, 0);
    EXPECT_EQ(cacheDiffer, 0);
}

// 51.13 look capture helper: the lake (both flags on) nearest the bench camera (-1728, -1320), a real shore point on it, and a camera
// 30 m back from the shore looking across the water. Prints the pose for water_skin_river_live.py --pose.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagLakeShorePose) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));
    const HydrologyMap& h = *g.hydrology();
    const WaterBodyIndex& wb = *g.waterBodies();
    const glm::vec2 cam(-1728.0f, -1320.0f);
    const WaterBodyIndex::Body* best = nullptr; float bestD = 1e30f;
    for (const auto& b : wb.bodies()) {
        if (b.cls == WaterBodyIndex::Class::Ocean || b.areaCells < 2) continue;
        const glm::vec2 c(h.originX() + (b.bboxMin.x + b.bboxMax.x + 1) * 0.5f * h.cellSize(),
                          h.originZ() + (b.bboxMin.y + b.bboxMax.y + 1) * 0.5f * h.cellSize());
        const float d = glm::length(c - cam);
        if (d < bestD) { bestD = d; best = &b; }
    }
    ASSERT_NE(best, nullptr);
    const int x0 = static_cast<int>(h.originX() + best->bboxMin.x * h.cellSize()) - 64;
    const int z0 = static_cast<int>(h.originZ() + best->bboxMin.y * h.cellSize()) - 64;
    const int w = std::min(768, static_cast<int>((best->bboxMax.x - best->bboxMin.x + 1) * h.cellSize()) + 128);
    const int d = std::min(768, static_cast<int>((best->bboxMax.y - best->bboxMin.y + 1) * h.cellSize()) + 128);
    std::vector<WaterSpan> sp; std::vector<uint8_t> has;
    g.waterSpansForBlock(x0, z0, w, d, sp, has);
    int wx = INT_MIN, wz = INT_MIN; float wd = 1e30f; long wet = 0;
    for (int z = 0; z < d; ++z) for (int x = 0; x < w; ++x) {
        const size_t i = static_cast<size_t>(z) * w + x;
        if (!has[i] || std::fabs(sp[i].topY - best->level) > 0.01f) continue;
        ++wet;
        const float dd = glm::length(glm::vec2(x0 + x, z0 + z) - cam);
        if (dd < wd) { wd = dd; wx = x0 + x; wz = z0 + z; }
    }
    std::printf("body %d (%s) level %.2f area %d cells, %ld wet columns at its level in the window; nearest wet (%d, %d)\n", best->id,
                best->cls == WaterBodyIndex::Class::Lake ? "lake" : "pond", best->level, best->areaCells, wet, wx, wz);
    ASSERT_NE(wx, INT_MIN);
    const glm::vec2 dir = glm::normalize(cam - glm::vec2(wx, wz));
    glm::vec2 p(wx, wz); int shoreX = wx, shoreZ = wz;
    for (int s = 0; s < 400; ++s) {
        p += dir;
        const int px = static_cast<int>(std::lround(p.x)), pz = static_cast<int>(std::lround(p.y));
        WaterSpan one;
        if (!g.waterSpanAt(px, pz, one) && g.sampleSurface(px, pz).surfaceY + 1 >= best->level) { shoreX = px; shoreZ = pz; break; }
    }
    const glm::vec2 c = glm::vec2(shoreX, shoreZ) + dir * 30.0f;
    const int ground = g.sampleSurface(static_cast<int>(c.x), static_cast<int>(c.y)).surfaceY + 1;
    const float camY = std::max(best->level + 8.0f, static_cast<float>(ground) + 3.0f);
    const float yaw = std::atan2(-dir.y, -dir.x) * 57.29578f;
    std::printf("shore (%d, %d), ground there %d; camera ground %d\nPOSE %.1f,%.1f,%.1f,%.1f,%.1f\n", shoreX, shoreZ,
                g.sampleSurface(shoreX, shoreZ).surfaceY + 1, ground, c.x, camY, c.y, yaw, -14.0f);
}

// 51.14 measurement: the river spans' own edges (D4's bed + carve depth). With both flags on, in both regions: every face where a
// river-span column's water stands above a neighbour's ground with no water there (an edge), by the neighbour's kind and the
// height; and the river-to-river water steps (a lower river span beside a higher one: a vertical face of water over water).
TEST(WaterGeneratedContainmentTest, DISABLED_DiagRiverSpanEdges) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));
    struct Region { const char* name; int x0, z0; } regions[2] = {{"north", -2112, -3136}, {"51.4c view", -2112, -1216}};
    const int W = 768;
    const char* stageEnv = std::getenv("PHYXEL_STAGE");   // 1: river spans topped at the valley floor (the 51.14 candidate)
    const bool stage = stageEnv && stageEnv[0] == '1';
    for (const auto& r : regions) {
        std::vector<WaterSpan> sp; std::vector<uint8_t> has;
        g.waterSpansForBlock(r.x0, r.z0, W, W, sp, has);
        std::vector<WorldGenerator::ColumnSample> cs(static_cast<size_t>(W) * W);
        for (int z = 0; z < W; ++z) for (int x = 0; x < W; ++x) cs[static_cast<size_t>(z) * W + x] = g.sampleSurface(r.x0 + x, r.z0 + z);
        long sh[5] = {0, 0, 0, 0, 0}; float worstS = 0;
        if (stage) {
            for (size_t i = 0; i < cs.size(); ++i) {
                if (cs[i].riverOrder < 3) continue;
                WaterSpan rs; rs.bottomY = cs[i].surfaceY + 1; rs.topY = cs[i].valleyFloor;
                has[i] = rs.depth() >= kMinSpanDepth ? 1 : 0;
                if (has[i]) sp[i] = rs;
            }
            for (int z = 0; z + 1 < W; ++z) for (int x = 0; x + 1 < W; ++x) {
                const size_t i = static_cast<size_t>(z) * W + x;
                if (cs[i].riverOrder < 3) continue;
                for (size_t j : {i + 1, i + static_cast<size_t>(W)}) {
                    if (cs[j].riverOrder < 3) continue;
                    const float g2 = std::fabs(cs[i].valleyFloor - cs[j].valleyFloor);
                    sh[g2 < 0.1f ? 0 : g2 < 0.25f ? 1 : g2 < 0.5f ? 2 : g2 < 1.0f ? 3 : 4]++; worstS = std::max(worstS, g2);
                }
            }
            std::printf("[%s] STAGE: valley-floor difference between river neighbours <0.1 %ld | <0.25 %ld | <0.5 %ld | <1 %ld | >=1 %ld; worst %.2f\n",
                        r.name, sh[0], sh[1], sh[2], sh[3], sh[4], worstS);
        }
        long riverCols = 0, riverWet = 0, riverNoSpan = 0;
        long edgeBy[3] = {0, 0, 0};   // neighbour: river column without a span / creek or plain bank / lake-wet? (n/a)
        long hist[5] = {0, 0, 0, 0, 0};   // <0.5, <1, <2, <3, >=3 m
        long stepHist[5] = {0, 0, 0, 0, 0};
        float worstEdge = 0, worstStep = 0; int ex = 0, ez = 0, sx = 0, sz = 0;
        double sumTilt = 0; long nTilt = 0;
        static const int kN[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        auto bucket = [](float h) { return h < 0.5f ? 0 : h < 1.0f ? 1 : h < 2.0f ? 2 : h < 3.0f ? 3 : 4; };
        for (int z = 0; z < W; ++z) for (int x = 0; x < W; ++x) {
            const size_t i = static_cast<size_t>(z) * W + x;
            if (cs[i].riverOrder < 3) continue;
            ++riverCols;
            if (!has[i]) { ++riverNoSpan; continue; }
            ++riverWet;
            const float T = sp[i].topY;
            for (const auto& n : kN) {
                const int nx = x + n[0], nz = z + n[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= W) continue;
                const size_t j = static_cast<size_t>(nz) * W + nx;
                if (has[j]) {
                    if (cs[j].riverOrder >= 3) { const float dT = T - sp[j].topY; if (dT > 0) { sumTilt += dT; ++nTilt; } }
                    if (sp[j].topY < T - 0.25f) {
                        const float h = T - std::max(sp[j].topY, static_cast<float>(cs[j].surfaceY + 1));
                        if (h > 0.25f) { stepHist[bucket(h)]++; if (h > worstStep) { worstStep = h; sx = r.x0 + nx; sz = r.z0 + nz; } }
                    }
                    continue;
                }
                const float h = T - static_cast<float>(cs[j].surfaceY + 1);
                if (h <= kMinSpanDepth) continue;
                edgeBy[cs[j].riverOrder >= 3 ? 0 : 1]++;
                hist[bucket(h)]++;
                if (h > worstEdge) { worstEdge = h; ex = r.x0 + nx; ez = r.z0 + nz; }
            }
        }
        std::printf("[%s] river columns %ld: with a span %ld, without %ld; mean |dT| between river neighbours %.2f m\n", r.name,
                    riverCols, riverWet, riverNoSpan, nTilt ? sumTilt / nTilt : 0.0);
        std::printf("  edges (water over dry ground): beside a span-less river column %ld, beside a bank %ld; worst %.2f m at (%d, %d)\n",
                    edgeBy[0], edgeBy[1], worstEdge, ex, ez);
        std::printf("  edge heights <0.5 %ld | 0.5-1 %ld | 1-2 %ld | 2-3 %ld | >=3 %ld\n", hist[0], hist[1], hist[2], hist[3], hist[4]);
        std::printf("  water-over-water steps >0.25 m: <0.5 %ld | 0.5-1 %ld | 1-2 %ld | 2-3 %ld | >=3 %ld; worst %.2f m at (%d, %d)\n",
                    stepHist[0], stepHist[1], stepHist[2], stepHist[3], stepHist[4], worstStep, sx, sz);
        if (worstEdge > 0) {
            std::printf("  around the worst edge (x across, z down): surfaceY+1 / order / span top\n");
            for (int dz = -3; dz <= 3; ++dz) {
                std::printf("   ");
                for (int dx = -3; dx <= 3; ++dx) {
                    const int lx = ex - r.x0 + dx, lz = ez - r.z0 + dz;
                    if (lx < 0 || lz < 0 || lx >= W || lz >= W) { std::printf("        -        "); continue; }
                    const size_t k = static_cast<size_t>(lz) * W + lx;
                    if (has[k]) std::printf(" %4d/%d/%6.2f", cs[k].surfaceY + 1, cs[k].riverOrder, sp[k].topY);
                    else        std::printf(" %4d/%d/  dry ", cs[k].surfaceY + 1, cs[k].riverOrder);
                }
                std::printf("\n");
            }
        }
    }
}

// 51.14 measurement: why the river bed sits at the valley floor - the carve depth against the bed, column by column.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagRiverBedAgainstTheFloor) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));
    long n = 0, hist[6] = {0, 0, 0, 0, 0, 0}; double sumD = 0, sumRel = 0; float maxD = 0;
    int shown = 0;
    for (int z = -1216; z < -448; ++z) for (int x = -2112; x < -1344; ++x) {
        const auto c = g.sampleSurface(x, z);
        if (c.riverOrder < 3) continue;
        ++n; sumD += c.channelDepth; maxD = std::max(maxD, c.channelDepth);
        const float above = static_cast<float>(c.surfaceY + 1) - c.valleyFloor;   // bed top over the stage
        sumRel += c.bedHeight + c.channelDepth - c.valleyFloor;                  // the relief term left at this column
        hist[above < -2 ? 0 : above < -1 ? 1 : above < 0 ? 2 : above < 1 ? 3 : above < 3 ? 4 : 5]++;
        if (c.channelDepth > 0.9f * maxD && shown < 8 && (x + z) % 37 == 0) {
            ++shown;
            std::printf("  (%d, %d) order %d depth %.2f valleyFloor %.2f bedHeight %.2f surfaceY+1 %d shoreLift %d\n", x, z, c.riverOrder,
                        c.channelDepth, c.valleyFloor, c.bedHeight, c.surfaceY + 1, c.shoreLift);
        }
    }
    std::printf("river columns %ld: mean carve depth %.2f (max %.2f), mean relief term left %.2f\n", n, sumD / n, maxD, sumRel / n);
    std::printf("bed top - valley floor: <-2 %ld | -2..-1 %ld | -1..0 %ld | 0..1 %ld | 1..3 %ld | >=3 %ld\n", hist[0], hist[1], hist[2], hist[3],
                hist[4], hist[5]);
}

// WaterCore.md 51.14 - RIVERS HOLD LEVEL WATER. With every new-world flag on (drain, cell-minimum flood, level rivers), in both
// RiverCarve regions: no river water stands over dry ground (an edge) or over lower water by more than 0.25 m (a step), and the
// lakes stay wall-free. The control (level rivers off) reproduces the D4 draped film: thousands of steps.
TEST(WaterGeneratedContainmentTest, RiversHoldLevelWater) {
    struct Region { const char* name; int x0, z0; } regions[2] = {{"north", -2112, -3136}, {"51.4c view", -2112, -1216}};
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
        ASSERT_TRUE(loadRiverCarve(g, true, true, false));
        for (const auto& r : regions) {
            const Census c = census(g, r.x0, r.z0, 768, 768);
            report(r.name, r.x0, r.z0, 768, 768, c);
            ASSERT_GT(c.riverSteps + c.riverSpanEdges, 1000) << "control: the D4 river steps must reproduce in " << r.name;
        }
    }
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    for (const auto& r : regions) {
        const Census c = census(g, r.x0, r.z0, 768, 768);
        report(r.name, r.x0, r.z0, 768, 768, c);
        EXPECT_EQ(c.riverSpanEdges, 0) << r.name << ": worst river edge " << c.worstRiverSpanEdge << " m at (" << c.rex << ", " << c.rez << ")";
        EXPECT_EQ(c.riverSteps, 0) << r.name << ": worst river step " << c.worstRiverStep << " m at (" << c.rsx << ", " << c.rsz << ")";
        EXPECT_EQ(c.wallRiver + c.wallDrains + c.wallUnreached, 0)
            << r.name << ": worst lake wall at (" << c.wx << ", " << c.wz << "): level " << c.wLevel << " (" << c.wWhy << ")";
        // The river's CENTRE stays wet. (The first prediction, >= 90 % of every carved column, was wrong: level water fills the low
        // part of the channel and the relief left toward its edges stands as bank - 41-44 % of all carved columns hold water at the
        // 1 m cut limit, DiagRiverCutLimit. The width is a look trade against bank height, the owner's call.)
        long centre = 0, centreWet = 0, lifted = 0; int maxLift = 0;
        for (int z = r.z0; z < r.z0 + 768; ++z) for (int x = r.x0; x < r.x0 + 768; ++x) {
            const auto cs = g.sampleSurface(x, z);
            if (cs.riverOrder >= 3 && cs.channelDepth > 0.8f) {
                ++centre;
                if (cs.riverStage - static_cast<float>(cs.surfaceY + 1) >= kMinSpanDepth) ++centreWet;
            }
            if ((x - r.x0) % 2 == 0 && (z - r.z0) % 2 == 0 && cs.shoreLift > 0) { ++lifted; maxLift = std::max(maxLift, cs.shoreLift); }
        }
        std::printf("  %s: river centre band (carve depth > 0.8) %ld columns, wet %ld (%.0f %%); all carved columns wet %.0f %%\n", r.name,
                    centre, centreWet, 100.0 * centreWet / std::max(1L, centre), 100.0 * c.riverWet / std::max(1L, c.riverColumns));
        EXPECT_GE(centreWet * 10, centre * 9) << r.name << ": the river's centre must stay wet";
        std::printf("  %s: shore + bank guarantees lifted %ld of %d sampled columns, largest %d m\n", r.name, lifted, 384 * 384, maxLift);
    }
}

// 51.14 - the level river and its bank guarantee are the same chunked or whole: a window across the 51.4c view's worst D4 edge,
// spans in 32 x 32 blocks against one pass, and the column cache against the direct sample.
TEST(WaterGeneratedContainmentTest, TheLevelRiverIsTheSameChunkedOrWhole) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    const int x0 = -1792, z0 = -1280, W = 128;
    std::vector<WaterSpan> whole; std::vector<uint8_t> hasWhole;
    g.waterSpansForBlock(x0, z0, W, W, whole, hasWhole);
    long differ = 0, staged = 0, cacheDiffer = 0;
    for (int bz = 0; bz < W; bz += 32) for (int bx = 0; bx < W; bx += 32) {
        std::vector<WaterSpan> part; std::vector<uint8_t> hasPart;
        g.waterSpansForBlock(x0 + bx, z0 + bz, 32, 32, part, hasPart);
        for (int z = 0; z < 32; ++z) for (int x = 0; x < 32; ++x) {
            const size_t i = static_cast<size_t>(bz + z) * W + (bx + x), k = static_cast<size_t>(z) * 32 + x;
            if (hasWhole[i] != hasPart[k] || (hasWhole[i] && (whole[i].topY != part[k].topY || whole[i].bottomY != part[k].bottomY))) ++differ;
        }
    }
    const auto floorDiv = [](int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    for (int cz = floorDiv(z0, 32); cz < floorDiv(z0 + W, 32); ++cz) for (int cx = floorDiv(x0, 32); cx < floorDiv(x0 + W, 32); ++cx) {
        const auto cols = g.columnsForChunkForTest(glm::ivec2(cx, cz));
        for (int lx = 0; lx < 32; ++lx) for (int lz = 0; lz < 32; ++lz) {
            const auto& a = (*cols)[static_cast<size_t>(lx) * 32 + lz];
            const auto b = g.sampleSurface(cx * 32 + lx, cz * 32 + lz);
            if (a.surfaceY != b.surfaceY || a.riverStage != b.riverStage || a.shoreLift != b.shoreLift || a.creekBed != b.creekBed) ++cacheDiffer;
            if (b.riverOrder >= 3 && b.riverStage > -1e29f) ++staged;
        }
    }
    std::printf("window x %d z %d (%d^2): %ld river columns with a stage; %ld differ chunked vs whole, %ld cache vs direct\n", x0, z0, W,
                staged, differ, cacheDiffer);
    EXPECT_GT(staged, 100) << "the window must hold a river";
    EXPECT_EQ(differ, 0);
    EXPECT_EQ(cacheDiffer, 0);
}

// 51.14 - the flag off leaves the D4 river: every river column's span is bed + carve depth and no column carries a stage.
TEST(WaterGeneratedContainmentTest, TheLevelRiversFlagOffLeavesD4sRiver) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true, false));
    const int x0 = -1792, z0 = -1280, W = 128;
    std::vector<WaterSpan> sp; std::vector<uint8_t> has;
    g.waterSpansForBlock(x0, z0, W, W, sp, has);
    long river = 0, bad = 0;
    for (int z = 0; z < W; ++z) for (int x = 0; x < W; ++x) {
        const auto c = g.sampleSurface(x0 + x, z0 + z);
        if (c.riverStage > -1e29f) ++bad;
        if (c.riverOrder < 3) continue;
        ++river;
        const size_t i = static_cast<size_t>(z) * W + x;
        if (has[i] && (sp[i].bottomY != c.surfaceY + 1 || sp[i].topY != static_cast<float>(c.surfaceY + 1) + c.channelDepth)) ++bad;
    }
    std::printf("flag off: %ld river columns, %ld not as D4 made them\n", river, bad);
    EXPECT_GT(river, 100);
    EXPECT_EQ(bad, 0);
}

// 51.14 measurement: the river's wet share against the cut limit (kMaxRiverCut). An uncut river column today has its carved top more
// than 1 m over the stage; it would be cut (and wet) at a limit c when that excess is <= c.
// ===== 51.17: the bake stored per world (the owner's decision 2026-11-14: "paid once") =====
namespace {
constexpr uint32_t kRcSeed = 20260729u;
std::string tempHydroStore(const char* tag) {
    return (std::filesystem::temp_directory_path() /
            (std::string("phyxel_hydro_") + tag + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".hydro")).string();
}
struct RmStore { std::string p; ~RmStore() { std::error_code ec; std::filesystem::remove(p, ec); } };
// One "session": the in-process cache cleared first, so the bake comes from the file or from scratch - never from memory.
void storedSession(WorldGenerator& g, const std::string& path) {
    WorldGenerator::clearHydroBakeCache();
    g.setBakeStore(path);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
}
long levelsDiffer(const WorldGenerator& a, const WorldGenerator& b) {
    const auto& la = a.hydrology()->levels();
    const auto& lb = b.hydrology()->levels();
    if (la.size() != lb.size()) return -1;
    long d = 0;
    for (size_t i = 0; i < la.size(); ++i) if (la[i] != lb[i]) ++d;
    return d;
}
long spansDiffer(WorldGenerator& a, WorldGenerator& b, int x0, int z0, int w, int d) {
    std::vector<WaterSpan> sa, sb; std::vector<uint8_t> ha, hb;
    a.waterSpansForBlock(x0, z0, w, d, sa, ha);
    b.waterSpansForBlock(x0, z0, w, d, sb, hb);
    long diff = 0;
    if (sa.size() != sb.size() || ha != hb) return -1;
    for (size_t i = 0; i < sa.size(); ++i) if (sa[i].bottomY != sb[i].bottomY || sa[i].topY != sb[i].topY) ++diff;
    return diff;
}
void printStore(const char* name, const WorldGenerator& g) {
    const auto& s = g.bakeStoreStats();
    std::printf("[bake store] %s: %s, load %.1f ms, verify %.1f ms (%d minima, %d drain), %d drain cells, %ld bytes | bake %.2f s: "
                "flow %.2f, minima %.2f, flood %.2f, drain %.2f, bodies %.2f\n", name, WorldGenerator::bakeStoreStateName(s.state),
                s.loadMs, s.verifyMs, s.verifiedMinima, s.verifiedDrain, s.drainCells, s.fileBytes, g.bakeSeconds(),
                g.bakePhaseSeconds(0), g.bakePhaseSeconds(1), g.bakePhaseSeconds(2), g.bakePhaseSeconds(3), g.bakePhaseSeconds(4));
}
// The file's layout (format 1): "PXHB", uint32 format, uint64 key, int32 n, then n*n float cell minima, then uint32 count and
// count x {int32 cell, float surface}.
constexpr size_t kMinimaOffset = 4 + 4 + 8 + 4;
void patchFloat(const std::string& path, size_t offset, float delta) {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    ASSERT_TRUE(f.good());
    float v = 0.0f;
    f.seekg(static_cast<std::streamoff>(offset)); f.read(reinterpret_cast<char*>(&v), 4);
    v += delta;
    f.seekp(static_cast<std::streamoff>(offset)); f.write(reinterpret_cast<const char*>(&v), 4);
}
}  // namespace

// (a) A second session reads the file: the bake is the same, bit for bit, and the expensive phases are gone.
TEST(WaterGeneratedContainmentTest, TheStoredBakeEqualsAFreshOne) {
    const std::string path = tempHydroStore("eq");
    RmStore rm{path};
    WorldGenerator ref(WorldGenerator::GenerationType::Mountains, kRcSeed);
    storedSession(ref, "");   // no store: the reference bake
    printStore("reference (no store)", ref);
    EXPECT_EQ(ref.bakeStoreStats().state, WorldGenerator::BsOff);

    WorldGenerator s1(WorldGenerator::GenerationType::Mountains, kRcSeed);
    storedSession(s1, path);
    printStore("session 1 (no file)", s1);
    EXPECT_EQ(s1.bakeStoreStats().state, WorldGenerator::BsWritten);
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(levelsDiffer(ref, s1), 0);

    WorldGenerator s2(WorldGenerator::GenerationType::Mountains, kRcSeed);
    storedSession(s2, path);
    printStore("session 2 (the file)", s2);
    EXPECT_EQ(s2.bakeStoreStats().state, WorldGenerator::BsLoaded);
    EXPECT_EQ(s2.bakeStoreStats().verifiedMinima, WorldGenerator::kBakeVerifyMinima);
    EXPECT_GT(s2.bakeStoreStats().verifiedDrain, 0);
    EXPECT_LT(s2.bakePhaseSeconds(1) + s2.bakePhaseSeconds(3), 0.5) << "the minima and the drain must come from the file";
    const long dl = levelsDiffer(ref, s2);
    ASSERT_NE(ref.waterBodies(), nullptr); ASSERT_NE(s2.waterBodies(), nullptr);
    const size_t bodiesRef = ref.waterBodies()->bodies().size(), bodies2 = s2.waterBodies()->bodies().size();
    const long ds = spansDiffer(ref, s2, -2112, -1216, 256, 256);   // the 51.4c view
    std::printf("[bake store] session 2 vs reference: levels differ %ld of %zu, bodies %zu vs %zu, spans differ %ld (256 x 256)\n", dl,
                ref.hydrology()->levels().size(), bodies2, bodiesRef, ds);
    EXPECT_EQ(dl, 0);
    EXPECT_EQ(bodies2, bodiesRef);
    EXPECT_EQ(ds, 0);
    // The streaming generator's case: a second generator in the same process shares s2's bake and reports how it was made.
    WorldGenerator s3(WorldGenerator::GenerationType::Mountains, kRcSeed);
    s3.setBakeStore(path);
    ASSERT_TRUE(loadRiverCarve(s3, true, true, true));
    printStore("a second generator, same process", s3);
    EXPECT_TRUE(s3.bakeStoreStats().fromMemory);
    EXPECT_EQ(s3.bakeStoreStats().state, WorldGenerator::BsLoaded);
}

// (b) another key, (c) a verified cell wrong, (d) the known gap: an UNVERIFIED cell wrong is accepted - it may move a lake, but no
// wall of water may stand anywhere (the shore guarantee reads the live bake).
TEST(WaterGeneratedContainmentTest, TheStoredBakeTrustsItsKeyAndHoldsNoWall) {
    const std::string path = tempHydroStore("key");
    RmStore rm{path};
    WorldGenerator ref(WorldGenerator::GenerationType::Mountains, kRcSeed);
    storedSession(ref, path);   // writes the file; also the reference
    printStore("session 1", ref);
    ASSERT_TRUE(std::filesystem::exists(path));
    const std::string good = path + ".good";
    RmStore rmGood{good};
    std::filesystem::copy_file(path, good, std::filesystem::copy_options::overwrite_existing);

    // (b) the key: the bytes of another world's key
    {   // the key's first byte flipped
        std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(f.good());
        char b = 0; f.seekg(8); f.read(&b, 1); b = static_cast<char>(b ^ 0x5A); f.seekp(8); f.write(&b, 1);
    }
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, kRcSeed);
        storedSession(g, path);
        printStore("(b) another key", g);
        EXPECT_EQ(g.bakeStoreStats().state, WorldGenerator::BsRejectedKey);
        EXPECT_EQ(levelsDiffer(ref, g), 0);
    }
    // (c) a verified minimum 1 m off
    const int n = ref.hydrology()->cellsX();
    const std::vector<int> verified = WorldGenerator::bakeStoreVerifyCells(n);
    ASSERT_EQ(static_cast<int>(verified.size()), WorldGenerator::kBakeVerifyMinima);
    std::filesystem::copy_file(good, path, std::filesystem::copy_options::overwrite_existing);
    patchFloat(path, kMinimaOffset + 4 * static_cast<size_t>(verified[0]), 1.0f);
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, kRcSeed);
        storedSession(g, path);
        printStore("(c) a verified cell wrong", g);
        EXPECT_EQ(g.bakeStoreStats().state, WorldGenerator::BsRejectedVerify);
        EXPECT_EQ(levelsDiffer(ref, g), 0);
    }
    // (d) an unverified dry cell beside a lake (above sea), inside the 51.4c view, its minimum 6 m low
    const HydrologyMap* h = ref.hydrology();
    const auto& lv = h->levels();
    auto wet = [&](int i, int j) { return lv[static_cast<size_t>(j) * n + i] > HydrologyMap::NO_WATER * 0.5f; };
    int pick = -1; float best = 1e30f;   // the one nearest the 51.4c view's centre (-1728, -832)
    for (int j = 1; j < n - 1; ++j) for (int i = 1; i < n - 1; ++i) {
        if (wet(i, j) || std::find(verified.begin(), verified.end(), j * n + i) != verified.end()) continue;
        const int nb[4][2] = {{i + 1, j}, {i - 1, j}, {i, j + 1}, {i, j - 1}};
        bool edge = false;
        for (const auto& q : nb)
            if (wet(q[0], q[1]) && lv[static_cast<size_t>(q[1]) * n + q[0]] > h->seaLevel() + 0.01f) edge = true;
        if (!edge) continue;
        const float x = h->originX() + (i + 0.5f) * h->cellSize() + 1728.0f, z = h->originZ() + (j + 0.5f) * h->cellSize() + 832.0f;
        if (x * x + z * z < best) { best = x * x + z * z; pick = j * n + i; }
    }
    ASSERT_GE(pick, 0) << "no dry lake-edge cell in the view";
    std::filesystem::copy_file(good, path, std::filesystem::copy_options::overwrite_existing);
    patchFloat(path, kMinimaOffset + 4 * static_cast<size_t>(pick), -6.0f);
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, kRcSeed);
    storedSession(g, path);
    printStore("(d) an unverified cell 6 m low", g);
    EXPECT_EQ(g.bakeStoreStats().state, WorldGenerator::BsLoaded) << "the known gap: the store trusts its key past the verified cells";
    const int pi = pick % n, pj = pick / n;
    const int cx0 = static_cast<int>(h->originX() + pi * h->cellSize()), cz0 = static_cast<int>(h->originZ() + pj * h->cellSize());
    const long dl = levelsDiffer(ref, g);
    const int rx0 = cx0 - 320, rz0 = cz0 - 320;
    const Census c = census(g, rx0, rz0, 768, 768);
    report("(d) around the wrong cell", rx0, rz0, 768, 768, c);
    std::printf("[bake store] (d) cell (%d, %d) at (%d, %d): levels differ %ld cells; walls %ld\n", pi, pj, cx0, cz0, dl,
                c.wallRiver + c.wallDrains + c.wallUnreached);
    EXPECT_EQ(c.wallRiver + c.wallDrains + c.wallUnreached, 0) << "a stale stored minimum stood water as a wall";
}

TEST(WaterGeneratedContainmentTest, TheBakeStoreShipsOn) {
    EXPECT_TRUE(WorldGenerator::s_bakeStoreOn);
    EXPECT_EQ(WorldGenerator::kHydroBakeVersion, 1u);
}

// 51.17 (measure first): where a fresh bake's time goes on RiverCarve (all new-world flags on), and how many cells sit on a lake's
// edge (a wet cell with a dry 4-neighbour, or a dry cell with a wet one) - the cells whose minimum sets a level or a shore.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagBakeSplit) {
    WorldGenerator::clearHydroBakeCache();
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 1);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    const HydrologyMap* h = g.hydrology();
    const int n = h->cellsX();
    const auto& lv = h->levels();
    auto wet = [&](int i, int j) { return lv[static_cast<size_t>(j) * n + i] > HydrologyMap::NO_WATER * 0.5f; };
    long wetCells = 0, edge = 0, wetNotSea = 0;
    for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) {
        const bool w = wet(i, j);
        if (w) { ++wetCells; if (lv[static_cast<size_t>(j) * n + i] > h->seaLevel() + 0.01f) ++wetNotSea; }
        bool e = false;
        const int di[4] = {1, -1, 0, 0}, dj[4] = {0, 0, 1, -1};
        for (int k = 0; k < 4; ++k) {
            const int a = i + di[k], b = j + dj[k];
            if (a < 0 || b < 0 || a >= n || b >= n) continue;
            if (wet(a, b) != w) e = true;
        }
        if (e) ++edge;
    }
    std::printf("[bake split] threads %u, stride %d: bake %.2f s + flow %.2f s | minima %.2f, flood %.2f, drain %.2f, bodies %.2f s\n",
                std::thread::hardware_concurrency(), g.bakeMinStride(), g.bakeSeconds(), g.bakePhaseSeconds(0), g.bakePhaseSeconds(1),
                g.bakePhaseSeconds(2), g.bakePhaseSeconds(3), g.bakePhaseSeconds(4));
    std::printf("[bake split] cells %d, wet %ld (above sea %ld), on a lake's edge %ld (%.1f %%)\n", n * n, wetCells, wetNotSea,
                edge, 100.0 * edge / (n * n));
}

TEST(WaterGeneratedContainmentTest, DISABLED_DiagRiverCutLimit) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    struct Region { const char* name; int x0, z0; } regions[2] = {{"north", -2112, -3136}, {"51.4c view", -2112, -1216}};
    const float lim[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
    for (const auto& r : regions) {
        long n = 0, cut = 0, wouldBe[6] = {0, 0, 0, 0, 0, 0}, centreDry = 0, centre = 0;
        for (int z = r.z0; z < r.z0 + 768; ++z) for (int x = r.x0; x < r.x0 + 768; ++x) {
            const auto c = g.sampleSurface(x, z);
            if (c.riverOrder < 3 || c.riverStage < -1e29f) continue;
            ++n;
            const float e = static_cast<float>(c.surfaceY + 1) - c.riverStage;
            if (c.channelDepth > 0.8f) { ++centre; if (e > 1.0f) ++centreDry; }
            if (e <= 0.0f) { ++cut; for (auto& w : wouldBe) ++w; continue; }
            for (int k = 0; k < 6; ++k) if (e <= lim[k]) ++wouldBe[k];
        }
        std::printf("[%s] staged river columns %ld, wet now %ld; centre band (depth > 0.8) %ld, of it dry %ld\n", r.name, n, cut, centre, centreDry);
        for (int k = 0; k < 6; ++k) std::printf("  cut limit %.1f m: wet %ld (%.0f %%)\n", lim[k], wouldBe[k], 100.0 * wouldBe[k] / n);
    }
}

// 51.14 measurement: a camera pose for the look - low over a level river reach near the bench camera, looking along the water.
TEST(WaterGeneratedContainmentTest, DISABLED_DiagRiverPose) {
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true, true));
    const glm::vec2 cam(-1728.0f, -1320.0f);
    // the wet river column nearest the camera whose 31 x 31 neighbourhood is the most wet (a broad reach, not a sliver)
    int bx = INT_MIN, bz = INT_MIN; long bestWet = 0; float bestD = 1e30f;
    for (int z = -1700; z < -900; z += 4) for (int x = -2100; x < -1350; x += 4) {
        const auto c = g.sampleSurface(x, z);
        if (c.riverOrder < 3 || c.riverStage - static_cast<float>(c.surfaceY + 1) < 0.3f) continue;
        long wet = 0;
        for (int dz = -15; dz <= 15; dz += 3) for (int dx = -15; dx <= 15; dx += 3) {
            const auto n = g.sampleSurface(x + dx, z + dz);
            if (n.riverOrder >= 3 && n.riverStage - static_cast<float>(n.surfaceY + 1) >= 0.05f) ++wet;
        }
        const float d = glm::length(glm::vec2(x, z) - cam);
        if (wet > bestWet + 5 || (wet + 5 >= bestWet && d < bestD)) { if (wet > bestWet) bestWet = wet; bx = x; bz = z; bestD = d; }
    }
    ASSERT_NE(bx, INT_MIN);
    // the reach's direction: principal axis of the wet columns within 40 m
    double sx = 0, sz = 0, sxx = 0, szz = 0, sxz = 0; long n = 0;
    for (int dz = -40; dz <= 40; dz += 2) for (int dx = -40; dx <= 40; dx += 2) {
        const auto c = g.sampleSurface(bx + dx, bz + dz);
        if (c.riverOrder < 3 || c.riverStage - static_cast<float>(c.surfaceY + 1) < 0.05f) continue;
        sx += dx; sz += dz; sxx += dx * dx; szz += dz * dz; sxz += dx * dz; ++n;
    }
    const double mx = sx / n, mz = sz / n, cxx = sxx / n - mx * mx, czz = szz / n - mz * mz, cxz = sxz / n - mx * mz;
    const double ang = 0.5 * std::atan2(2 * cxz, cxx - czz);
    const glm::vec2 along(static_cast<float>(std::cos(ang)), static_cast<float>(std::sin(ang)));
    const auto c0 = g.sampleSurface(bx, bz);
    const glm::vec2 p = glm::vec2(bx, bz) - along * 28.0f;
    const int ground = g.sampleSurface(static_cast<int>(p.x), static_cast<int>(p.y)).surfaceY + 1;
    const float camY = std::max(c0.riverStage + 5.0f, static_cast<float>(ground) + 2.5f);
    const float yaw = std::atan2(along.y, along.x) * 57.29578f;
    std::printf("reach at (%d, %d): stage %.2f, bed %d, %ld of 121 wet nearby, %ld wet within 40 m\nPOSE %.1f,%.1f,%.1f,%.1f,%.1f\n", bx, bz,
                c0.riverStage, c0.surfaceY + 1, bestWet, n, p.x, camY, p.y, yaw, -12.0f);
}
