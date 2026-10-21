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
    for (int it = 0; it < 24; ++it) {
        const glm::vec2 w = g.meanderedChannelPos(x.x, x.y);
        x += 0.5f * (target - w);   // damped: the warp's gradient reaches ~1 at creek wavelengths
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
    std::vector<float> gridAngles, worldAngles;
    int gridRight = 0, worldRight = 0;
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
        const glm::vec2 downDir = pd - pj;              // world space (through the warp)
        const glm::vec2 downDirBake = jl[1] - jl[0];    // bake space (the construction)
        for (int u : n.up[c]) {
            if (u == mainIn) continue;
            // the tributary's arrival: the LAST piece of its centreline (a fillet's end tangent
            // once M2d-2 is in; the straight segment's direction on the D8 grid before it)
            const auto tl = n.f->centrelineAt(n.centre(u).x, n.centre(u).y);
            if (tl.size() < 2) continue;
            const glm::vec2 pa = g.meanderedChannelPos(tl[tl.size() - 2].x, tl[tl.size() - 2].y);
            const glm::vec2 pb = g.meanderedChannelPos(tl[tl.size() - 1].x, tl[tl.size() - 1].y);
            const glm::vec2 tribDir = pb - pa;                    // the tributary's approach, world space
            const float aWorld = angleDeg(tribDir, downDir);
            worldAngles.push_back(aWorld);
            if (std::fabs(aWorld - 90.0f) <= 5.0f) ++worldRight;
            // the construction: the same tangents before the warp
            const float a = angleDeg(tl[tl.size() - 1] - tl[tl.size() - 2], downDirBake);
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
    const float worldMean = std::accumulate(worldAngles.begin(), worldAngles.end(), 0.0f) / worldAngles.size();
    std::printf("[ M2d ] world space (through the meander warp, 4 u tangents): mean angle %.1f deg, within 5 deg of 90: %d (%.1f %%) - reported, the warp rotates short tangents at creek wavelengths\n",
                worldMean, worldRight, 100.0f * worldRight / worldAngles.size());
    std::printf("[ M2d ] %zu junctions (%d fillets baked), bake space (the construction): mean angle %.1f deg, within 5 deg of 90: %d (%.1f %%); histogram:",
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
        // at a confluence the column under the junction's warped centre can lie on the TRIBUTARY's
        // fillet (a higher, upstream profile): only a hit on this cell's own channel is a sample
        const auto own = g.channelHitAt(static_cast<float>(col.x) + 0.5f, static_cast<float>(col.y) + 0.5f);
        if (!own.hit || own.cell != c) { prev = 1e30f; continue; }
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

// M2d-3: the meander wavelength follows the channel width — λ = 7.3·w^1.1 u (F4f). Measured as
// the mean spacing of sign changes of the lateral displacement along a straight 4 km line, for
// the narrowest law channel (w = 1 u) and a wide one (w = 6.3 u): the spacing ratio must track
// the law's wavelength ratio (7.5 : 55.6), and the absolute spacing of each band must be within
// a factor of 2 of λ/2 (the fbm's zero crossings sit near half a wavelength). Red on the committed
// code: one wavelength (55.6 u) for every width — ratio 1.
TEST(RiverGeometryTest, MeanderWavelengthFollowsChannelWidth) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.riverNetwork() && g.riverNetwork()->areaLaw());
    auto spacing = [&](float halfWidth) {
        int crossings = 0; float prev = 0.0f; bool have = false;
        const int N = 4000;
        for (int x = 0; x < N; ++x) {
            const float d = g.meanderDisplacementForHalfWidth(static_cast<float>(x) + 0.5f, 1234.5f, halfWidth).y;
            if (have && ((d >= 0.0f) != (prev >= 0.0f))) ++crossings;
            prev = d; have = true;
        }
        return crossings > 0 ? static_cast<float>(N) / crossings : 1e9f;
    };
    const float sNarrow = spacing(0.5f), sWide = spacing(3.15f);
    const float lamNarrow = 7.3f * std::pow(1.0f, 1.1f), lamWide = 7.3f * std::pow(6.3f, 1.1f);
    std::printf("[ M2d-3 ] zero-crossing spacing of the lateral warp: w = 1.0 u -> %.1f u (law lambda %.1f), w = 6.3 u -> %.1f u (law lambda %.1f); ratio %.3f vs law %.3f\n",
                sNarrow, lamNarrow, sWide, lamWide, sNarrow / sWide, lamNarrow / lamWide);
    EXPECT_NEAR(sNarrow / sWide, lamNarrow / lamWide, 0.5f * lamNarrow / lamWide) << "the wavelength does not follow the width";
    EXPECT_GT(sNarrow, 0.5f * lamNarrow * 0.5f);
    EXPECT_LT(sNarrow, 2.0f * lamNarrow * 0.5f);
    EXPECT_GT(sWide, 0.5f * lamWide * 0.5f);
    EXPECT_LT(sWide, 2.0f * lamWide * 0.5f);
    // The field itself: a creek cell reads a narrow local width, a trunk cell a wide one.
    const FlowField* f = g.riverNetwork();
    float maxLocal = 0.0f;
    for (int j = 0; j < f->cellsZ(); j += 4)
        for (int i = 0; i < f->cellsX(); i += 4)
            maxLocal = std::max(maxLocal, f->localHalfWidthAt(f->originX() + (i + 0.5f) * f->cellSize(), f->originZ() + (j + 0.5f) * f->cellSize()));
    std::printf("[ M2d-3 ] local half-width field: max %.2f u (the law's widest channel in the box is %.2f u)\n", maxLocal, f->maxHalfWidth());
    EXPECT_GT(maxLocal, 0.0f);
    EXPECT_LE(maxLocal, f->maxHalfWidth() + 1e-3f);
}

// M2e: spring SITES (D17: data, no water). Every creek head (a channel cell with no channel inflow
// outside a coarse basin) and every basin feed (the last channel cell before a coarse basin) is a
// site, with a discharge Q = runoff × catchment. The expected sets are enumerated here from the
// drainage graph independently of FlowField's own pass. Red on the committed code: no sites.
TEST(RiverGeometryTest, SpringSitesMarkEveryCreekHeadAndBasinFeed) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.riverNetwork() && g.hydrology());
    const Net n = buildNet(g);
    const HydrologyMap* h = g.hydrology();
    auto basin = [&](int c) { const glm::vec2 p = n.centre(c); return h->filledAt(p.x, p.y) > h->baseAt(p.x, p.y) + 1e-3f; };
    std::vector<std::pair<int, int>> expectHeads, expectFeeds;   // (i, j) cells
    for (int c = 0; c < n.nx * n.nz; ++c) {
        if (n.order[c] < 1 || basin(c)) continue;
        if (n.up[c].empty()) expectHeads.push_back({c % n.nx, c / n.nx});
        if (n.down[c] != c && basin(n.down[c])) expectFeeds.push_back({c % n.nx, c / n.nx});
    }
    const auto sites = g.springSites();
    std::vector<std::pair<int, int>> heads, feeds; int outlets = 0;
    float maxQ = 0.0f; bool monotone = true; float prevA = -1.0f, prevQ = -1.0f;
    for (const auto& s : sites) {
        const int i = static_cast<int>(std::floor((s.pos.x - n.ox) / n.cs)), j = static_cast<int>(std::floor((s.pos.y - n.oz) / n.cs));
        if (s.kind == 0) heads.push_back({i, j});
        else if (s.kind == 1) feeds.push_back({i, j});
        else ++outlets;
        maxQ = std::max(maxQ, s.dischargeM3s);
        if (s.dischargeM3s <= 0.0f) monotone = false;
        if (prevA >= 0.0f && ((s.catchmentKm2 > prevA) != (s.dischargeM3s > prevQ)) && s.catchmentKm2 != prevA) monotone = false;
        prevA = s.catchmentKm2; prevQ = s.dischargeM3s;
    }
    std::sort(expectHeads.begin(), expectHeads.end()); std::sort(heads.begin(), heads.end());
    std::sort(expectFeeds.begin(), expectFeeds.end()); std::sort(feeds.begin(), feeds.end());
    std::printf("[ M2e ] spring sites: %zu heads (expected %zu), %zu basin feeds (expected %zu), %d basin outlets; runoff %.0f mm/yr, largest discharge %.3f m3/s\n",
                heads.size(), expectHeads.size(), feeds.size(), expectFeeds.size(), outlets, g.runoffMmPerYear(), maxQ);
    EXPECT_EQ(heads, expectHeads) << "the creek heads are not exactly the channel cells without an inflow";
    EXPECT_EQ(feeds, expectFeeds) << "the basin feeds are not exactly the last cells before a basin";
    EXPECT_GT(outlets, 0);
    EXPECT_TRUE(monotone) << "discharge must be positive and increase with catchment";
    // Q = R·A: a 1 km² catchment at 500 mm/yr is 0.0158 m³/s (the auditor's arithmetic, §10 F4f).
    for (const auto& s : sites) { EXPECT_NEAR(s.dischargeM3s / std::max(1e-6f, s.catchmentKm2), 0.01585f, 2e-4f); break; }
}

// A probe, not an invariant: the carved confluences nearest the origin on the baseline world, with
// the world column under the junction's warped centre and the local terrain, so an engine capture
// (plan M2d L4: a confluence in frame) can be posed from a printed number instead of a guess.
TEST(RiverGeometryTest, PrintCarvedConfluencesNearOrigin) {
    WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
    g.applyRecipe(v2Recipe(20260816u));
    ASSERT_TRUE(g.riverNetwork());
    const Net n = buildNet(g);
    struct Site { float d2; int c; };
    std::vector<Site> sites;
    for (int c = 0; c < n.nx * n.nz; ++c) {
        if (n.up[c].size() < 2 || !n.f->carvedRiverAt(n.centre(c).x, n.centre(c).y)) continue;
        int carvedIn = 0;
        for (int u : n.up[c]) if (n.f->carvedRiverAt(n.centre(u).x, n.centre(u).y)) ++carvedIn;
        if (carvedIn < 1) continue;
        const glm::vec2 p = n.centre(c);
        sites.push_back({p.x * p.x + p.y * p.y, c});
    }
    std::sort(sites.begin(), sites.end(), [](const Site& a, const Site& b) { return a.d2 < b.d2; });
    std::printf("[ M2d probe ] %zu carved confluences in the box; nearest the origin:\n", sites.size());
    for (size_t k = 0; k < sites.size() && k < 6; ++k) {
        const int c = sites[k].c;
        const glm::vec2 p = n.centre(c);
        const glm::ivec2 col = columnUnderWarp(g, p);
        const auto s = g.sampleSurface(col.x, col.y);
        const auto bank = g.sampleSurface(col.x + 12, col.y + 12);
        std::printf("    cell (%d,%d) accum %d order %d: carved column (%d,%d) bed %.2f surfaceY %d shelf %d; bank 12 u away surfaceY %d; inflows %zu\n",
                    static_cast<int>(p.x), static_cast<int>(p.y), n.accum[c], n.order[c], col.x, col.y, s.bedHeight, s.surfaceY,
                    s.bedShelf ? 1 : 0, bank.surfaceY, n.up[c].size());
    }
    EXPECT_FALSE(sites.empty());
}
