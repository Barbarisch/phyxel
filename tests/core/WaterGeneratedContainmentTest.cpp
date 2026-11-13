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
bool loadRiverCarve(WorldGenerator& g, bool drains = false, bool floods = false) {
    std::ifstream f("tests/core/golden/rivercarve_recipe.json");
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    WorldRecipe r = WorldRecipe::fromJson(ss.str());
    r.carveDrainsBasins = drains;
    r.bakeFloodsTerrain = floods;
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
    float worstBy[3] = {0, 0, 0}; int wbx[3] = {0, 0, 0}, wbz[3] = {0, 0, 0}, wbg[3] = {0, 0, 0}; float wbl[3] = {0, 0, 0};
};

void report(const char* name, int x0, int z0, int w, int d, const Census& c) {
    std::printf("[%s] region x %d..%d z %d..%d\n", name, x0, x0 + w, z0, z0 + d);
    std::printf("  wet %ld | wall faces: river %ld, drains %ld, unreached %ld | steps %ld\n", c.wet, c.wallRiver, c.wallDrains,
                c.wallUnreached, c.steps);
    std::printf("  river-span edges (not lake walls) %ld, worst %.1f m at (%d, %d)\n", c.riverSpanEdges, c.worstRiverSpanEdge, c.rex, c.rez);
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
    for (int z = 0; z < d; ++z) for (int x = 0; x < w; ++x) {
        const auto cs = g.sampleSurface(x0 + x, z0 + z);
        surf[static_cast<size_t>(z) * w + x] = cs.surfaceY; order[static_cast<size_t>(z) * w + x] = cs.riverOrder;
    }
    Census c;
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
            const float groundTop = static_cast<float>(surf[j]) + 1.0f;
            if (has[j]) {
                if (spans[j].topY < T - 0.25f && order[i] < 3 && order[j] < 3 && groundTop < T - 0.25f) ++c.steps;
                continue;
            }
            if (groundTop >= T - kMinSpanDepth) continue;   // the ground holds it
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

// 51.13 - the acceptance test, step 1 of 2. The bbox-corner region north of the 51.4c lake, where 51.12 left walls with no channel
// near (drain on: drains 1,450 / unreached 485 faces, worst 35.6 m). Control: the drain on, the new flag off - must reproduce them.
// Prediction (written before the build): with the bake flooding the generated surface the drains + unreached faces fall by more than
// half and the worst lake wall falls below 20 m. What remains is the terrain dipping INSIDE a cell (step 2), reported, not hidden.
TEST(WaterGeneratedContainmentTest, TheBakeFloodsTheGeneratedSurface) {
    const int x0 = -2112, z0 = -3136, w = 768, d = 768;
    long before = 0;
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
        ASSERT_TRUE(loadRiverCarve(g, true, false));
        const Census c = census(g, x0, z0, w, d);
        report("control: drain on, floods off", x0, z0, w, d, c);
        before = c.wallDrains + c.wallUnreached;
        ASSERT_GT(before, 1000) << "control: 51.12's residual walls must reproduce (drains 1,450 + unreached 485)";
        ASSERT_GT(c.worstWall, 30.0f);
    }
    WorldGenerator g(WorldGenerator::GenerationType::Mountains, 20260729u);
    ASSERT_TRUE(loadRiverCarve(g, true, true));
    const Census c = census(g, x0, z0, w, d);
    report("drain on, floods on", x0, z0, w, d, c);
    EXPECT_LT(c.wallDrains + c.wallUnreached, before / 2);
    EXPECT_EQ(c.wallRiver, 0);
    EXPECT_LT(c.worstWall, 20.0f) << "worst wall at (" << c.wx << ", " << c.wz << "): level " << c.wLevel << ", ground top "
                                  << c.wGround + 1 << " (" << c.wWhy << ")";
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
