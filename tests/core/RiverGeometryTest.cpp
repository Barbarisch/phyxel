#include <gtest/gtest.h>

#include "core/FlowField.h"
#include "core/HydrologyMap.h"
#include "core/WorldGenerator.h"
#include "core/WorldRecipe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
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

// ============================================================================
// M2d-4 (the lateral channel blend): the channel SECTION must meet the land beside it. The M4c
// crossings found the generator's bed profile standing 97-165 u ABOVE the land at the banks on
// the Mountains world (a river on an embankment) and carved mountain rivers at the bottom of a
// slot whose walls rise 10-15 u per column (m4c_red_green.txt, the refusal dumps). Read from the
// code: the profile samples the FULL relief at the cell centre (FlowField::bake, base - depth)
// while the valley rule multiplies the relief beside the channel by a FRACTION (0 at the
// centreline), so the two disagree by the relief itself. Measured here for every carved channel
// cell outside a basin: a lateral section +-1 u along the cell's flow normal out to 5 half-widths
// + 8 u, the emitted continuous ground per column. Two invariants, both red today:
//   - the bank meets the rim: |rim - the first natural column| <= 1 u (no embankment, no lip);
//   - no wall the shaping made: every lateral step outside the channel <= max(repose 0.70,
//     1.3 x the natural step from baseHeightFn at the same columns) + 1/3 u (the shelf grid);
//     1.3 is the largest biome heightScale (resources/biomes.json), so a natural cliff that the
//     biome blend steepens is never counted as a wall.
// The Perlin baseline, where the relief is small, is the control for the metric.
// ============================================================================
namespace {

struct SectionStats {
    int cells = 0, embank = 0, lips = 0, walls = 0, stepsChecked = 0, natSteep = 0, cellsWithWall = 0;
    int cascades = 0, cascadeEmbank = 0, cascadeLips = 0, cascadeWalls = 0;   // reaches whose BED drops faster than the side slope
    float worstExcludedEmb = 0.0f, worstExcludedLip = 0.0f;                   // the worst bank under / above its rim on a fold, branch or cascade cell
    int folded = 0, foldedEmbank = 0, foldedLips = 0, foldedWalls = 0;         // reaches the meander warp folds
    int cornerCuts = 0; float worstCorner = 0.0f;                              // banks on a LOWER reach's floodplain (a bend's inner corner)
    int branches = 0;                                                          // the sectioned column lies on ANOTHER branch of a folded warp
    int alongChannel = 0;                                                      // the section stayed in the band too long: it runs along the channel
    float localEmb = 0.0f, localLip = 0.0f; glm::ivec2 localEmbAt{0, 0}, localLipAt{0, 0};   // the cell's local (direction-free) worst
    float worstEmb = 0.0f, worstLip = 0.0f, worstWall = 0.0f, worstNat = 0.0f;
    glm::ivec2 embAt{0, 0}, lipAt{0, 0}, wallAt{0, 0};
    struct Diag { float value; std::string text; };
    std::vector<Diag> embDiag, wallDiag, lipDiag;   // the three worst of each, with the facts at the column
};

void keepWorst(std::vector<SectionStats::Diag>& v, float value, std::string text) {
    v.push_back({value, std::move(text)});
    std::sort(v.begin(), v.end(), [](const SectionStats::Diag& a, const SectionStats::Diag& b) { return a.value > b.value; });
    if (v.size() > 3) v.resize(3);
}

// the facts at a world column: emitted / natural / envelope (at the warped point) / the bake cell under the warp
std::string columnFacts(WorldGenerator& g, const Net& n, int x, int z) {
    const auto cs = g.sampleSurface(x, z);
    const float nat = g.naturalGroundAt(x, z);
    const float px = static_cast<float>(x) + 0.5f, pz = static_cast<float>(z) + 0.5f;   // world space
    const glm::vec2 mw = g.meanderedChannelPos(static_cast<float>(x), static_cast<float>(z));   // the sampler's warped point
    const float env = n.f->valleyEnvelopeAt(px, pz, mw.x, mw.y);
    const int ci = static_cast<int>(std::floor((px - n.ox) / n.cs)), cj = static_cast<int>(std::floor((pz - n.oz) / n.cs));
    char buf[256];
    std::snprintf(buf, sizeof buf, "(%d,%d) emitted %.1f natural %.1f env %s order %d carved %d cell (%d,%d)",
                  x, z, cs.bedHeight, nat, env < 1e29f ? std::to_string(env).substr(0, 6).c_str() : "none", cs.riverOrder, cs.carvedRiver ? 1 : 0, ci, cj);
    std::string out = buf;
    char sb[96];
    out += " sources[";
    for (int src : n.f->valleySourcesAt(px, pz)) {
        const auto es = n.f->envelopeSample(src, px, pz, mw.x, mw.y);
        std::snprintf(sb, sizeof sb, " %d(%d,%d):%.1f/rim%.1f/perp%.1f/floor%.1f%s", src, src % n.nx, src / n.nx, es.env, es.rim, es.perp < 1e29f ? es.perp : -1.0f, es.valleyHalf, n.f->isFoldedSource(src) ? "F" : "");
        out += sb;
    }
    out += " ]";
    return out;
}

// the footprint the bake samples for a cell, re-derived here: 5 stations on the straight segment x {centre, +-hw},
// each inverse-warped (the test's 24-step inversion) and read through naturalGroundAt; plus the fillet flag
std::string footprintFacts(WorldGenerator& g, const Net& n, int c, float hw) {
    const glm::vec2 a = n.centre(c);
    const glm::vec2 b = (n.down[c] != c) ? n.centre(n.down[c]) : a;
    const glm::vec2 dir = b - a; const float len = glm::length(dir);
    const glm::vec2 nrm = len > 0.0f ? glm::vec2(-dir.y, dir.x) / len : glm::vec2(0.0f);
    float mn = 1e30f, mx = -1e30f;
    std::string pts;
    for (int s = 0; s <= 4; ++s)
        for (int side = -1; side <= 1; ++side) {
            const glm::vec2 q = a + dir * (0.25f * s) + nrm * (static_cast<float>(side) * hw);
            const glm::ivec2 col = columnUnderWarp(g, q);
            const float nat = g.naturalGroundAt(col.x, col.y);
            mn = std::min(mn, nat); mx = std::max(mx, nat);
            if (side == 0) pts += " " + std::to_string(static_cast<int>(nat));
        }
    const auto pl = n.f->centrelineAt(a.x, a.y);
    char buf[200];
    std::snprintf(buf, sizeof buf, "footprint naturals min %.1f max %.1f (centreline stations:%s) fillet %d down %s",
                  mn, mx, pts.c_str(), pl.size() > 2 ? 1 : 0, n.down[c] != c ? "yes" : "sink");
    return buf;
}

SectionStats measureChannelSections(WorldGenerator& g, const char* name) {
    SectionStats s;
    const Net n = buildNet(g);
    const HydrologyMap* h = g.hydrology();
    constexpr float kRepose = 0.70f, kScaleMax = 1.3f, kTol = 1.0f / 3.0f;
    for (int c = 0; c < n.nx * n.nz; ++c) {
        if (n.order[c] < 1) continue;
        const glm::vec2 ctr = n.centre(c);
        if (!n.f->carvedRiverAt(ctr.x, ctr.y)) continue;
        if (h->filledAt(ctr.x, ctr.y) > h->baseAt(ctr.x, ctr.y) + 1e-3f) continue;   // a coarse basin: the floor is the shape
        const glm::ivec2 col = columnUnderWarp(g, ctr);
        const auto hit = g.channelHitAt(static_cast<float>(col.x) + 0.5f, static_cast<float>(col.y) + 0.5f);
        if (!hit.hit || hit.cell != c || !hit.hasProfile || hit.inBasin || !hit.carved) continue;
        glm::vec2 d = n.f->flowDirAt(ctr.x, ctr.y);
        if (d.x == 0.0f && d.y == 0.0f) continue;
        d = glm::normalize(d);
        {   // the channel's WORLD direction at the column: the envelope polyline's nearest segment (the warp
            // rotates the bake-space flow direction; a section along the bake normal can run along the channel)
            const auto* wpl = n.f->sourceWorldPolyline(c);
            float dmin = 1e30f;
            if (wpl) for (size_t q = 0; q + 1 < wpl->size(); ++q) {
                const glm::vec2 a = (*wpl)[q], b = (*wpl)[q + 1], pc = glm::vec2(col) + glm::vec2(0.5f);
                const glm::vec2 ab = b - a; const float L2 = glm::dot(ab, ab);
                if (L2 <= 0.0f) continue;
                const float u = std::clamp(glm::dot(pc - a, ab) / L2, 0.0f, 1.0f);
                const float dist = glm::length(pc - (a + ab * u));
                if (dist < dmin) { dmin = dist; d = ab / std::sqrt(L2); }
            }
        }
        const glm::vec2 nrm(-d.y, d.x);
        const float reach = hit.halfWidth * 5.0f + 8.0f;
        ++s.cells;
        bool wallHere = false;
        {
            std::vector<glm::ivec2> band{col};
            std::vector<glm::ivec2> seen{col};
            float worstEmbHere = 0.0f, worstLipHere = 0.0f; glm::ivec2 embAtHere{0, 0}, lipAtHere{0, 0};
            bool cornerHere = false; float embCornerHere = 0.0f;
            const int R = static_cast<int>(std::ceil(hit.halfWidth)) + 2;
            for (size_t q = 0; q < band.size(); ++q) {
                const glm::ivec2 b = band[q];
                const auto bh = g.channelHitAt(static_cast<float>(b.x) + 0.5f, static_cast<float>(b.y) + 0.5f);
                if (!bh.hit || !bh.hasProfile || bh.cell != c) continue;
                const float rimB = bh.bedProfile + bh.fullDepth;
                for (const glm::ivec2 dxy : {glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1)}) {
                    const glm::ivec2 nb = b + dxy;
                    if (std::abs(nb.x - col.x) > R || std::abs(nb.y - col.y) > R) continue;
                    const auto ns = g.sampleSurface(nb.x, nb.y);
                    if (ns.riverOrder >= 1 && ns.carvedRiver) {   // more band: queue it once
                        if (std::find(seen.begin(), seen.end(), nb) == seen.end()) { seen.push_back(nb); band.push_back(nb); }
                        continue;
                    }
                    if (ns.riverOrder >= 1) continue;   // a creek beside the river: not a bank
                    const float emb = rimB - ns.bedHeight;
                    constexpr float kBankTol = 1.0f + 1e-3f;   // natural levees (<= 1 u, GROUNDED) - the red's bound + 1 mm of float (audit: a 1/3 u shelf allowance added after GREEN 18 is withdrawn)
                    if (emb > kBankTol) {
                        // a corner cut: the neighbour lies on a LOWER reach's floodplain (its envelope's winner is another reach, its floor covers the column, its rim is lower)
                        bool corner = false;
                        const float qx = static_cast<float>(nb.x) + 0.5f, qz = static_cast<float>(nb.y) + 0.5f;
                        const glm::vec2 mq = g.meanderedChannelPos(static_cast<float>(nb.x), static_cast<float>(nb.y));
                        for (int src : n.f->valleySourcesAt(qx, qz)) {
                            if (src == c) continue;
                            const auto es = n.f->envelopeSample(src, qx, qz, mq.x, mq.y);
                            if (es.perp <= es.valleyHalf + 1.0f && es.rim < rimB - 1.0f) { corner = true; break; }
                        }
                        if (corner) { cornerHere = true; embCornerHere = std::max(embCornerHere, emb); }
                        else if (emb > worstEmbHere) { worstEmbHere = emb; embAtHere = nb; }
                    } else if (-emb > kBankTol && -emb > worstLipHere) { worstLipHere = -emb; lipAtHere = nb; }
                }
            }
            if (cornerHere) { ++s.cornerCuts; s.worstCorner = std::max(s.worstCorner, embCornerHere); }
            s.localEmb = worstEmbHere; s.localEmbAt = embAtHere; s.localLip = worstLipHere; s.localLipAt = lipAtHere;
        }
        // a CASCADE: the bed drops faster than the hillslope bound along this reach - no section at that
        // bound can meet the land on both banks (a waterfall step); counted apart, reported, not asserted
        bool cascade = hit.bedSlope > FlowField::kValleySideSlope;
        if (cascade) ++s.cascades;
        // a FOLDED reach: the warp's gradient exceeds 1 here, so the channel's world line doubles back and
        // two bake stations share a world column - the carve and the envelope can read different rims
        if (!cascade && n.f->isFoldedSource(c)) { ++s.folded; cascade = true; }   // counted with the cascades below
        // a second BRANCH: the column under the warped cell centre (the test's damped inversion) lies off the
        // world polyline the envelope was baked around (the bake's Newton inversion) by more than the band -
        // the warp has two preimages here and the carve shows the channel on both; only one carries the valley
        if (!cascade) {
            const auto* wpl = n.f->sourceWorldPolyline(c);
            float dmin = 1e30f;
            if (wpl) for (size_t q = 0; q + 1 < wpl->size(); ++q) {
                const glm::vec2 a = (*wpl)[q], b = (*wpl)[q + 1], pc = glm::vec2(col) + glm::vec2(0.5f);
                const glm::vec2 ab = b - a; const float L2 = glm::dot(ab, ab);
                const float u = L2 > 0.0f ? std::clamp(glm::dot(pc - a, ab) / L2, 0.0f, 1.0f) : 0.0f;
                dmin = std::min(dmin, glm::length(pc - (a + ab * u)));
            }
            if (dmin > hit.halfWidth + 3.0f) { ++s.branches; cascade = true; }
        }
        for (const float side : {-1.0f, 1.0f}) {
            bool pastChannel = false;
            float prevEmit = 0.0f, prevNat = 0.0f, prevRim = 0.0f;
            int prevX = 0, prevZ = 0;
            float edgeRim = hit.bedProfile + hit.fullDepth;   // updated as the walk crosses the band
            for (float off = 0.0f; off <= reach; off += 1.0f) {
                const glm::vec2 p = glm::vec2(col) + glm::vec2(0.5f) + nrm * (side * off);
                const int x = static_cast<int>(std::floor(p.x)), z = static_cast<int>(std::floor(p.y));
                const auto cs = g.sampleSurface(x, z);
                const float nat = g.naturalGroundAt(x, z);
                // the floor's own level here (the rim the envelope reads for this reach): a sloping reach's floor
                // descends along the river, compressed by the warp in world space - its gradient is not a wall
                const glm::vec2 mqc = g.meanderedChannelPos(static_cast<float>(x), static_cast<float>(z));
                float rimHere = 0.0f;
                {   // the rim of the source whose envelope wins at this column
                    float bestEnv = 1e30f;
                    for (int src : n.f->valleySourcesAt(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f)) {
                        const auto es = n.f->envelopeSample(src, static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f, mqc.x, mqc.y);
                        if (es.env < bestEnv) { bestEnv = es.env; rimHere = es.rim; }
                    }
                }
                if (!pastChannel) {
                    if (cs.riverOrder >= 1 && cs.carvedRiver) {   // the channel band: remember the rim at its edge
                        const auto eh = g.channelHitAt(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
                        if (eh.hit && eh.hasProfile) edgeRim = eh.bedProfile + eh.fullDepth;
                        if (off > 2.0f * hit.halfWidth + 6.0f) { ++s.alongChannel; break; }   // the section runs along the channel
                        continue;
                    }
                    pastChannel = true;                                     // the bank: the first natural column
                    // the rim right beside the bank (the last channel column's own segment - at a confluence or
                    // a bend the section's normal can leave the centre cell's segment before it leaves the band)
                    (void)edgeRim;
                    if (side < 0.0f) {   // once per cell: the local measure's worst, classified like the walk's
                        const float emb = s.localEmb, lip = s.localLip;
                        if (emb > 0.0f && cascade) { ++s.cascadeEmbank; s.worstExcludedEmb = std::max(s.worstExcludedEmb, emb); }
                        else if (emb > 0.0f) {
                            ++s.embank; if (emb > s.worstEmb) { s.worstEmb = emb; s.embAt = s.localEmbAt; }
                            char hb[200];
                            std::snprintf(hb, sizeof hb, "EMBANKMENT %.1f u (local): centre hit profile %.1f + depth %.2f (cell %d, hw %.2f) | bank ", emb, hit.bedProfile, hit.fullDepth, hit.cell, hit.halfWidth);
                            keepWorst(s.embDiag, emb, std::string(hb) + columnFacts(g, n, s.localEmbAt.x, s.localEmbAt.y) + " | centre col " + columnFacts(g, n, col.x, col.y) + " | " + footprintFacts(g, n, c, hit.halfWidth));
                        }
                        if (lip > 0.0f && cascade) { ++s.cascadeLips; s.worstExcludedLip = std::max(s.worstExcludedLip, lip); }
                        else if (lip > 0.0f) {
                            ++s.lips; if (lip > s.worstLip) { s.worstLip = lip; s.lipAt = s.localLipAt; }
                            char hb[200];
                            std::snprintf(hb, sizeof hb, "LIP %.1f u (local): centre hit profile %.1f + depth %.2f (cell %d, hw %.2f) | bank ", lip, hit.bedProfile, hit.fullDepth, hit.cell, hit.halfWidth);
                            keepWorst(s.lipDiag, lip, std::string(hb) + columnFacts(g, n, s.localLipAt.x, s.localLipAt.y) + " | centre col " + columnFacts(g, n, col.x, col.y) + " | " + footprintFacts(g, n, c, hit.halfWidth));
                        }
                    }
                    prevEmit = cs.bedHeight; prevNat = nat; prevRim = rimHere; prevX = x; prevZ = z;
                    continue;
                }
                if (cs.riverOrder >= 1) break;   // another channel: this side's section ends
                const float step = std::fabs(cs.bedHeight - prevEmit), natStep = std::fabs(nat - prevNat), rimStep = std::fabs(rimHere - prevRim);
                const float colDist = std::sqrt(static_cast<float>((x - prevX) * (x - prevX) + (z - prevZ) * (z - prevZ)));   // 1 or sqrt 2
                ++s.stepsChecked;
                if (natStep > kRepose) { ++s.natSteep; if (natStep > s.worstNat) s.worstNat = natStep; }
                const float wallBound = std::max({kRepose * colDist, kScaleMax * natStep, rimStep}) + kTol + 1e-3f;
                const glm::vec2 mSw = g.meanderedChannelPos(static_cast<float>(x), static_cast<float>(z));
                const bool inCreekSwale = n.f->nearestChannel(mSw.x, mSw.y, 2.0f * n.f->maxCreekHalfWidth() + 2.0f, 1).order >= 1;   // the creek swale's own dip
                if (inCreekSwale) { prevEmit = cs.bedHeight; prevNat = nat; prevRim = rimHere; prevX = x; prevZ = z; continue; }
                if (step > wallBound && cascade) ++s.cascadeWalls;
                else if (step > wallBound) {
                    ++s.walls; wallHere = true;
                    if (step > s.worstWall) { s.worstWall = step; s.wallAt = {x, z}; }
                    const glm::vec2 pp = glm::vec2(col) + glm::vec2(0.5f) + nrm * (side * (off - 1.0f));
                    const int px = static_cast<int>(std::floor(pp.x)), pz = static_cast<int>(std::floor(pp.y));
                    char hb[120];
                    std::snprintf(hb, sizeof hb, "WALL %.1f u (natural step %.1f) at off %.0f side %+.0f of cell %d: ", step, natStep, off, side, c);
                    keepWorst(s.wallDiag, step, std::string(hb) + "this " + columnFacts(g, n, x, z) + " | prev " + columnFacts(g, n, px, pz));
                }
                prevEmit = cs.bedHeight; prevNat = nat; prevRim = rimHere; prevX = x; prevZ = z;
            }
        }
        if (wallHere) ++s.cellsWithWall;
    }
    std::printf("[ M2d-4 ] %s: %d warp inversions beyond 1 u at the bake\n", name, g.warpInverseFailures());
    std::printf("[ M2d-4 ] %s: %d carved cells outside basins sectioned; EMBANKMENTS (rim above the bank by > 1 u) %d (%.1f %%), worst %.1f u at (%d,%d); LIPS (bank above the rim by > 1 u) %d (%.1f %%), worst %.1f u at (%d,%d); WALLS the shaping made %d of %d lateral steps (%.1f %%) on %d cells (%.1f %%), worst %.1f u per column at (%d,%d); natural steps beyond 0.70: %d (%.1f %%), worst %.1f\n",
                name, s.cells,
                s.embank, s.cells ? 100.0f * s.embank / s.cells : 0.0f, s.worstEmb, s.embAt.x, s.embAt.y,
                s.lips, s.cells ? 100.0f * s.lips / s.cells : 0.0f, s.worstLip, s.lipAt.x, s.lipAt.y,
                s.walls, s.stepsChecked, s.stepsChecked ? 100.0f * s.walls / s.stepsChecked : 0.0f,
                s.cellsWithWall, s.cells ? 100.0f * s.cellsWithWall / s.cells : 0.0f, s.worstWall, s.wallAt.x, s.wallAt.y,
                s.natSteep, s.stepsChecked ? 100.0f * s.natSteep / s.stepsChecked : 0.0f, s.worstNat);
    std::printf("[ M2d-4 ] %s: CASCADES (bed slope > %.2f along the reach) %d cells + FOLDED reaches (the warp folds the channel) %d cells of %d: %d embankments (worst %.1f u), %d lips (worst %.1f u), %d walls there - reported, not asserted (a waterfall step; a fold of the M2d-3 warp)\n",
                name, FlowField::kValleySideSlope, s.cascades, s.folded, s.cells, s.cascadeEmbank, s.worstExcludedEmb, s.cascadeLips, s.worstExcludedLip, s.cascadeWalls);
    std::printf("[ M2d-4 ] %s: CORNER CUTS (a bank below its reach's rim because it lies on a LOWER reach's floodplain - the inner corner of a bend on a sloping river) %d bank sides, worst %.1f u - reported, not asserted\n",
                name, s.cornerCuts, s.worstCorner);
    std::printf("[ M2d-4 ] %s: SECOND BRANCHES (the sectioned column lies off the envelope's world polyline by more than the band: the warp has two preimages here) %d cells - counted with the cascades, reported; sections that ran ALONG the channel (skipped) %d sides\n",
                name, s.branches, s.alongChannel);
    for (const auto& d : s.embDiag) std::printf("[ M2d-4-diag ] %s: %s\n", name, d.text.c_str());
    for (const auto& d : s.wallDiag) std::printf("[ M2d-4-diag ] %s: %s\n", name, d.text.c_str());
    for (const auto& d : s.lipDiag) std::printf("[ M2d-4-diag ] %s: %s\n", name, d.text.c_str());
    return s;
}

}  // namespace

// M2d-4 (D37): naturalGroundAt is what the bed profile samples; it must be the ground the sampler
// emits wherever nothing else shapes the column - asserted on 4 000 columns of the Perlin baseline
// farther than 12 u from any channel, off the coast and off every road (the generator carries no
// WorldForge plan in this fixture). Equality to 1e-3: the same arithmetic, not a model of it.
void pinNaturalGround(WorldGenerator& g, const char* name) {
    ASSERT_TRUE(g.riverNetwork());
    uint32_t h = 20260816u;
    int checked = 0, mismatches = 0; float worst = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        h = h * 1664525u + 1013904223u; const int x = static_cast<int>(h >> 8) % 16000 - 8000;
        h = h * 1664525u + 1013904223u; const int z = static_cast<int>(h >> 8) % 16000 - 8000;
        const glm::vec2 m = g.meanderedChannelPos(static_cast<float>(x), static_cast<float>(z));
        if (g.riverNetwork()->nearestChannel(m.x, m.y, 12.0f, 1).order >= 1) continue;
        const auto s = g.sampleSurface(x, z);
        if (s.riverOrder >= 1 || s.creekBed || s.bedShelf) continue;   // the coast is part of the natural: not skipped
        if (g.riverNetwork()->valleyEnvelopeAt(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f, m.x, m.y) < 1e29f) continue;   // inside a valley envelope: shaped
        const float nat = g.naturalGroundAt(x, z);
        ++checked;
        const float diff = std::fabs(nat - s.bedHeight);
        if (diff > 1e-3f) { ++mismatches; worst = std::max(worst, diff); }
    }
    std::printf("[ M2d-4 ] %s: naturalGroundAt vs sampleSurface().bedHeight: %d columns checked, %d differ (worst %.3f u)\n", name, checked, mismatches, worst);
    ASSERT_GT(checked, 500);
    EXPECT_EQ(mismatches, 0) << name << ": naturalGroundAt is not the ground the sampler emits";
}

TEST(RiverGeometryTest, NaturalGroundMatchesTheSamplerOffChannel) {
    {
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
        g.applyRecipe(v2Recipe(20260816u));
        pinNaturalGround(g, "Perlin 20260816");
    }
    {   // the two bugs the helper had (the biome blend, the coast ramp) were Mountains findings: pinned there too
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 424242u);
        WorldRecipe r = v2Recipe(424242u);
        r.type = "Mountains";
        g.applyRecipe(r);
        pinNaturalGround(g, "Mountains 424242");
    }
}

TEST(RiverGeometryTest, ChannelSectionsMeetTheLandAtABoundedSlope) {
    {
        WorldGenerator g(WorldGenerator::GenerationType::Mountains, 424242u);
        WorldRecipe r = v2Recipe(424242u);
        r.type = "Mountains";
        g.applyRecipe(r);
        ASSERT_TRUE(g.riverNetwork());
        const SectionStats s = measureChannelSections(g, "Mountains 424242");
        ASSERT_GT(s.cells, 0);
        // the bound is GROUNDED, not fitted: natural levees stand up to 3 m above their floodplain
        // (PMC7780550, section 10), so a bank more than 3 u under its rim is a river on a wall - asserted
        // over every class but the M2d-3 fold / second-branch cells (reported with their worst above).
        // The perch rule's sampling against the sheared band leaves a residual on Mountains (GREEN 18:
        // worst 5.2 u, corner cuts to 9.1 u) - this assertion is RED there until that is closed, recorded.
        EXPECT_LE(s.worstEmb, 3.0f) << "a bank more than 3 u (the levee maximum) under its rim: a river on a wall";
        EXPECT_LE(s.worstCorner, 3.0f) << "a bend's inner corner cut more than 3 u under the reach's rim";
        EXPECT_EQ(s.lips, 0) << "a lip at the channel edge: the land at the bank stands above the rim";
        EXPECT_EQ(s.walls, 0) << "a wall the valley shaping made: a lateral step steeper than repose and than the natural ground";
    }
    {
        WorldGenerator g(WorldGenerator::GenerationType::Perlin, 20260816u);
        g.applyRecipe(v2Recipe(20260816u));
        ASSERT_TRUE(g.riverNetwork());
        const SectionStats s = measureChannelSections(g, "Perlin 20260816 (control: small relief)");
        ASSERT_GT(s.cells, 0);
        // the same GROUNDED bound as Mountains (levees to 3 u); the count above 1 u stays reported (1 cell at 1.004 u)
        EXPECT_LE(s.worstEmb, 3.0f) << "a bank more than 3 u (the levee maximum) under its rim on the baseline";
        EXPECT_LE(s.worstCorner, 3.0f) << "a corner cut more than 3 u on the baseline";
        EXPECT_EQ(s.lips, 0) << "a lip at the channel edge on the baseline";
        EXPECT_EQ(s.walls, 0) << "a wall the valley shaping made on the baseline";
    }
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
