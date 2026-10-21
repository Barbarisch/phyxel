#include <gtest/gtest.h>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include <glm/glm.hpp>

using namespace Phyxel;

// ============================================================================
// RiverGeometryTest — M2d (docs/WorldForgeV2Plan.md, M2 engineering design): the SHAPE of the
// carved channels on a version-2 world, measured on the generator's own output (D17: shapes, not
// water). Two invariants with grounded targets:
//   • junction angles: Howard's rule cos θ = S_main / S_trib and the observed network MEANS of
//     45–72° (Seybold 2017, Hooshyar 2017 via ESurf 13:219; §10 F4f) — a D8 grid joins at 90°/0°
//     and the meander warp only bends that a little;
//   • the bed is monotone non-increasing downstream along a channel outside basins (a stream does
//     not flow uphill) — the carve is terrain minus a depth, and where the warped centreline
//     crosses a cell edge the carved bed can step up.
// Both are MEASURED here first (red on the committed code), then fixed by construction in M2d.
// ============================================================================
namespace {

WorldRecipe v2Recipe(uint32_t seed) {
    WorldRecipe r;
    r.version = 2;
    r.type = "Perlin";
    r.seed = seed;
    return r;
}

struct Cell { int i, j; };

// The world column whose meander-warped position lands on `target` (the warp is x + d(x) with a
// small, smooth d, so the fixed point x = target - d(x) converges in a few steps).
glm::ivec2 columnUnderWarp(const WorldGenerator& g, glm::vec2 target) {
    glm::vec2 x = target;
    for (int it = 0; it < 8; ++it) {
        const glm::vec2 w = g.meanderedChannelPos(x.x, x.y);
        x += target - w;
    }
    return glm::ivec2(static_cast<int>(std::floor(x.x)), static_cast<int>(std::floor(x.y)));
}

struct Net {
    const FlowField* f;
    int nx, nz; float cs, ox, oz;
    std::vector<int> down;      // downstream cell index or self
    std::vector<int> accum, order;
    std::vector<std::vector<int>> up;   // river inflows per cell
    glm::vec2 centre(int c) const { return {ox + (c % nx + 0.5f) * cs, oz + (c / nx + 0.5f) * cs}; }
};

Net buildNet(const WorldGenerator& g) {
    Net n;
    n.f = g.riverNetwork();
    n.nx = n.f->cellsX(); n.nz = n.f->cellsZ(); n.cs = n.f->cellSize(); n.ox = n.f->originX(); n.oz = n.f->originZ();
    const int N = n.nx * n.nz;
    n.down.assign(N, -1); n.accum.assign(N, 0); n.order.assign(N, 0); n.up.assign(N, {});
    for (int c = 0; c < N; ++c) {
        const glm::vec2 p = n.centre(c);
        n.order[c] = n.f->orderAt(p.x, p.y);
        n.accum[c] = n.f->accumAt(p.x, p.y);
        const glm::vec2 d = n.f->flowDirAt(p.x, p.y);
        if (d.x == 0.0f && d.y == 0.0f) { n.down[c] = c; continue; }
        const int di = static_cast<int>(std::lround(d.x)), dj = static_cast<int>(std::lround(d.y));
        const int ni = c % n.nx + di, nj = c / n.nx + dj;
        n.down[c] = (ni < 0 || nj < 0 || ni >= n.nx || nj >= n.nz) ? c : nj * n.nx + ni;
    }
    for (int c = 0; c < N; ++c)
        if (n.order[c] >= 1 && n.down[c] != c && n.order[n.down[c]] >= 1) n.up[n.down[c]].push_back(c);
    return n;
}

// Walk k cells upstream along the largest-accum inflow from c; returns the cell reached.
int walkUp(const Net& n, int c, int k) {
    for (int s = 0; s < k; ++s) {
        if (n.up[c].empty()) break;
        int best = n.up[c][0];
        for (int u : n.up[c]) if (n.accum[u] > n.accum[best]) best = u;
        c = best;
    }
    return c;
}
int walkDown(const Net& n, int c, int k) {
    for (int s = 0; s < k; ++s) { if (n.down[c] == c) break; c = n.down[c]; }
    return c;
}

float angleDeg(glm::vec2 a, glm::vec2 b) {
    const float la = glm::length(a), lb = glm::length(b);
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    return glm::degrees(std::acos(std::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f)));
}

}  // namespace

// Junction angle = the angle between the tributary's approach direction and the main stem's
// downstream direction, taken on the WARPED (carved) centreline 4 cells either side of the
// junction. Red: a D8 grid joins at 90° (or 0° head-on), the warp bends that by a few degrees.
TEST(RiverGeometryTest, NetworkMeanJunctionAngleIsInTheObservedBand) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.riverNetwork());
    const Net n = buildNet(g);
    std::vector<float> angles;
    int rightAngles = 0;   // within 5° of 90°
    std::map<int, int> hist;   // 10° bins
    std::vector<float> gridAngles;
    int gridRight = 0;
    for (int c = 0; c < n.nx * n.nz; ++c) {
        if (n.order[c] < 2 || n.up[c].size() < 2) continue;      // a confluence of two channels
        // main stem = largest-accum inflow; every other inflow is a tributary
        int mainIn = n.up[c][0];
        for (int u : n.up[c]) if (n.accum[u] > n.accum[mainIn]) mainIn = u;
        const int downC = walkDown(n, c, 4);
        if (downC == c) continue;                                 // junction at an outlet: no downstream direction
        // the main stem's departure: the first piece of the junction cell's own centreline, in
        // world space (both ends through the meander warp)
        const auto jl = n.f->centrelineAt(n.centre(c).x, n.centre(c).y);
        if (jl.size() < 2) continue;
        const glm::vec2 pj = g.meanderedChannelPos(jl[0].x, jl[0].y);
        const glm::vec2 pd = g.meanderedChannelPos(jl[1].x, jl[1].y);
        const glm::vec2 downDir = pd - pj;
        for (int u : n.up[c]) {
            if (u == mainIn) continue;
            // the tributary's arrival: the LAST piece of its centreline (a fillet's end tangent
            // once M2d-2 is in; the straight segment's direction on the D8 grid before it)
            const auto tl = n.f->centrelineAt(n.centre(u).x, n.centre(u).y);
            if (tl.size() < 2) continue;
            const glm::vec2 pa = g.meanderedChannelPos(tl[tl.size() - 2].x, tl[tl.size() - 2].y);
            const glm::vec2 pb = g.meanderedChannelPos(tl[tl.size() - 1].x, tl[tl.size() - 1].y);
            const glm::vec2 tribDir = pb - pa;                    // the tributary's approach
            const float a = angleDeg(tribDir, downDir);
            angles.push_back(a);
            ++hist[static_cast<int>(a / 10.0f) * 10];
            if (std::fabs(a - 90.0f) <= 5.0f) ++rightAngles;
            // CONTROL: the same junction on the raw D8 grid (cell centre → cell centre, no fillet,
            // no warp) — what the geometry was before M2d-2, measured in the same run.
            const float gridA = angleDeg(n.centre(c) - n.centre(u), n.centre(downC) - n.centre(c));
            gridAngles.push_back(gridA);
            if (std::fabs(gridA - 90.0f) <= 5.0f) ++gridRight;
        }
    }
    ASSERT_FALSE(angles.empty());
    const float gridMean = std::accumulate(gridAngles.begin(), gridAngles.end(), 0.0f) / gridAngles.size();
    std::printf("[ M2d ] control, raw D8 grid: mean angle %.1f deg, within 5 deg of 90: %d (%.1f %%)\n",
                gridMean, gridRight, 100.0f * gridRight / gridAngles.size());
    const float mean = std::accumulate(angles.begin(), angles.end(), 0.0f) / angles.size();
    std::printf("[ M2d ] %zu junctions (%d fillets baked): mean angle %.1f deg, within 5 deg of 90: %d (%.1f %%); histogram:",
                angles.size(), n.f->filletCount(), mean, rightAngles, 100.0f * rightAngles / angles.size());
    for (const auto& [b, k] : hist) std::printf(" %d-%d:%d", b, b + 10, k);
    std::printf("\n");
    EXPECT_GE(mean, 45.0f) << "network mean junction angle below the observed band (F4f: 45-72)";
    EXPECT_LE(mean, 72.0f) << "network mean junction angle above the observed band (F4f: 45-72)";
    EXPECT_LT(100.0f * rightAngles / angles.size(), 10.0f) << "right-angle junctions are a grid artefact";
}

// Along the longest channel (max accum chain), the carved bed sampled at the column under each
// cell centre's warped position never rises downstream between coarse basins (by more than 1/3 u,
// the carve grid). M2d-1 makes this true by construction: the bed is a longitudinal profile.
TEST(RiverGeometryTest, BedIsMonotoneAlongTheLongestChannel) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.riverNetwork());
    const Net n = buildNet(g);
    // the chain: start at the max-accum cell's farthest headwater, walk down to the outlet
    int mouth = 0;
    for (int c = 0; c < n.nx * n.nz; ++c) if (n.accum[c] > n.accum[mouth]) mouth = c;
    int head = mouth;
    while (!n.up[head].empty()) head = walkUp(n, head, 1);
    std::vector<int> chain;
    for (int c = head; ; c = n.down[c]) { chain.push_back(c); if (n.down[c] == c || chain.size() > 100000) break; }
    ASSERT_GT(chain.size(), 10u);
    int rises = 0, checked = 0, creekRises = 0, creekChecked = 0; float worstRise = 0.0f; int worstAt = -1;
    float prev = 1e30f; bool prevWet = true;
    for (int c : chain) {
        const glm::vec2 target = n.centre(c);
        const glm::ivec2 col = columnUnderWarp(g, target);
        const auto s = g.sampleSurface(col.x, col.y);
        if (s.riverOrder < 1) { prev = 1e30f; continue; }      // the warp missed the channel here: restart the run
        // a coarse basin cell (filled above its sampled height): the bed there is the basin floor,
        // the river enters and leaves it at its rim — the profile holds between basins
        const HydrologyMap* h = g.hydrology();
        const bool wet = h->filledAt(target.x, target.y) > h->baseAt(target.x, target.y) + 1e-3f;
        // STRICT on the carved reach (the bed IS the profile there); creeks only have their bumps
        // cut, so a dip on a creek line (a pond basin at voxel scale) may still end in a step up —
        // counted and printed, not asserted.
        const bool carvedCell = n.f->carvedRiverAt(target.x, target.y);
        if (prev < 1e29f && !wet && !prevWet) {
            const float rise = s.bedHeight - prev;
            if (!carvedCell) { ++creekChecked; if (rise > 1.0f / 3.0f + 1e-4f) ++creekRises; }
            else ++checked;
            if (carvedCell && rise > 1.0f / 3.0f + 1e-4f) {
                ++rises;
                if (rise > worstRise) { worstRise = rise; worstAt = c; }
                const auto hit = g.channelHitAt(static_cast<float>(col.x) + 0.5f, static_cast<float>(col.y) + 0.5f);
                std::printf("    rise %.2f u at column (%d,%d) [cell centre (%d,%d)]: bed %.2f -> %.2f; column order %d carved %d creek %d depth %.2f; hit order %d carved %d profile %.2f hasProfile %d inBasin %d; cell carved %d\n",
                            rise, col.x, col.y, static_cast<int>(target.x), static_cast<int>(target.y), prev, s.bedHeight,
                            s.riverOrder, s.carvedRiver ? 1 : 0, s.creekBed ? 1 : 0, s.channelDepth,
                            hit.order, hit.carved ? 1 : 0, hit.bedProfile, hit.hasProfile ? 1 : 0, hit.inBasin ? 1 : 0,
                            n.f->carvedRiverAt(target.x, target.y) ? 1 : 0);
            }
        }
        prev = s.bedHeight; prevWet = wet;
    }
    std::printf("[ M2d ] longest channel: %zu cells (accum %d at the mouth), carved reach: %d bed steps checked, %d rise downstream, worst rise %.2f u%s; creek reach: %d steps, %d rise (dips left as pond basins)\n",
                chain.size(), n.accum[mouth], checked, rises, worstRise,
                worstAt >= 0 ? (" at (" + std::to_string(static_cast<int>(n.centre(worstAt).x)) + "," + std::to_string(static_cast<int>(n.centre(worstAt).y)) + ")").c_str() : "",
                creekChecked, creekRises);
    ASSERT_GT(checked, 0) << "no carved reach on the longest channel";
    EXPECT_EQ(rises, 0) << "the carved bed rises downstream: a stream flowing uphill";
}
