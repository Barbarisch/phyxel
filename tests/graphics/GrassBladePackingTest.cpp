// Grass blades must not overlap — the geometric guarantee, measured on the real placement math.
//
// THE CONDITION. A blade is a vertical quad of width W rising from the voxel top face. Two blades
// with roots r_i, r_j and widths W_i, W_j intersect IFF
//
//     ||r_i - r_j||  <=  (W_i + W_j) / 2
//
// (worst-case yaw: each quad reaches W/2 from its root in the direction of the other). The
// symmetric form matters — neighbouring blades sit at different distances and so have different
// widths. This is a pure geometric predicate, which is what makes the whole feature testable here
// instead of by screenshot.
//
// WHY THIS FILE EXISTS. Blades were grouped into tufts of 7 whose roots jitter inside a FIXED
// +/-0.08u box regardless of blade width, and each voxel clamped its roots into its own
// [0.005,0.995] independently. Measured on the live engine (docs/evidence/pack_before.json):
// 28 blades resolved to 10 distinguishable regions and 112 blades to 8 — blade pixels grew 29x
// while visible structure saturated, the signature of blades landing on top of each other.
//
// RED-BEFORE-GREEN. Against the tuft placement these tests FAIL, reporting intra-tuft pairs at
// ~0.001-0.01u against a 0.040u blade, and cross-voxel-border pairs at ~0.01u. They go green when
// placement moves to the world-aligned lattice in shaders/grass_sites.glsl.
//
// Per the repo convention: call the PRODUCTION placement function for the thing under test
// (GrassRenderPipeline::bladeRootLocal), and re-derive independently only for the other side of a
// CPU/GPU mirror (see shaderDensityFrac in GrassDensityLodTest.cpp).

#include <gtest/gtest.h>

#include "graphics/GrassRenderPipeline.h"
#include "graphics/GrassSiteOrder.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

using Phyxel::Graphics::GrassRenderPipeline;

namespace {

constexpr float kRadius = 224.0f;
// NOT the shipped default (55 as of 2026-08-21; Params{} is the authority and
// PackingBudgetHoldsAtShippedDefaults reads it live). 140 is kept as a deliberately DENSER
// stress count — if packing holds at 140 it holds at every smaller count, since spacing is
// monotone in N.
constexpr uint32_t kDefaultBlades = 140;

struct Root {
    double x, z;      // ABSOLUTE world position (voxel origin + local), so cross-voxel pairs count
    float  width;
    int    vx, vz;
    uint32_t blade;
};

/// Every blade root over a block of voxels, in absolute world coordinates.
std::vector<Root> rootsOverBlock(int nVox, uint32_t blades, float dist, float widthScale = 1.0f) {
    std::vector<Root> out;
    out.reserve(static_cast<size_t>(nVox) * nVox * blades);
    const float w = GrassRenderPipeline::bladeWidthAt(dist, kRadius, blades, widthScale, true);
    for (int vx = 0; vx < nVox; ++vx)
        for (int vz = 0; vz < nVox; ++vz)
            for (uint32_t b = 0; b < blades; ++b) {
                const glm::vec2 l = GrassRenderPipeline::bladeRootLocal(vx, 60, vz, b, blades);
                out.push_back({static_cast<double>(vx) + l.x, static_cast<double>(vz) + l.y,
                               w, vx, vz, b});
            }
    return out;
}

struct Worst {
    double gap = 1e30;       // centre distance of the closest pair
    double required = 0.0;   // (Wi+Wj)/2 for that pair
    Root a{}, b{};
    bool found = false;
};

/// Closest pair via a uniform spatial hash. An O(n^2) sweep (the FaunaPlanTest pattern) is fine at
/// a few hundred points but this reaches ~1.3M pairs per configuration under Debug.
Worst closestPair(const std::vector<Root>& rs) {
    Worst w;
    if (rs.size() < 2) return w;
    double cell = 0.0;
    for (const auto& r : rs) cell = std::max(cell, static_cast<double>(r.width));
    cell = std::max(cell, 1e-4) * 2.0;

    std::unordered_map<long long, std::vector<int>> grid;
    auto key = [cell](double x, double z) {
        const long long ix = static_cast<long long>(std::floor(x / cell));
        const long long iz = static_cast<long long>(std::floor(z / cell));
        return (ix << 32) ^ (iz & 0xffffffffLL);
    };
    for (int i = 0; i < static_cast<int>(rs.size()); ++i) grid[key(rs[i].x, rs[i].z)].push_back(i);

    for (int i = 0; i < static_cast<int>(rs.size()); ++i) {
        const long long ix = static_cast<long long>(std::floor(rs[i].x / cell));
        const long long iz = static_cast<long long>(std::floor(rs[i].z / cell));
        for (long long dx = -1; dx <= 1; ++dx)
            for (long long dz = -1; dz <= 1; ++dz) {
                auto it = grid.find(((ix + dx) << 32) ^ ((iz + dz) & 0xffffffffLL));
                if (it == grid.end()) continue;
                for (int j : it->second) {
                    if (j <= i) continue;
                    const double ddx = rs[i].x - rs[j].x, ddz = rs[i].z - rs[j].z;
                    const double d = std::sqrt(ddx * ddx + ddz * ddz);
                    const double req = 0.5 * (rs[i].width + rs[j].width);
                    // Rank by how badly the pair violates, not by raw distance: a close pair of
                    // thin blades can be legal while a wider pair further apart is not.
                    if (d - req < w.gap - w.required) {
                        w = {d, req, rs[i], rs[j], true};
                    }
                }
            }
    }
    return w;
}

std::string describe(const Worst& w) {
    if (!w.found) return "no pairs";
    char buf[256];
    snprintf(buf, sizeof(buf),
             "closest pair %.5fu apart, needs > %.5fu  [voxel(%d,%d) blade %u  vs  voxel(%d,%d) blade %u]",
             w.gap, w.required, w.a.vx, w.a.vz, w.a.blade, w.b.vx, w.b.vz, w.b.blade);
    return buf;
}

} // namespace

// ── The guarantee, at the shipped configuration ───────────────────────────────────────────────

TEST(GrassBladePacking, RootsNeverOverlapAtFullDensity) {
    const auto rs = rootsOverBlock(4, kDefaultBlades, 0.0f);
    const Worst w = closestPair(rs);
    ASSERT_TRUE(w.found);
    EXPECT_GT(w.gap, w.required)
        << "blades overlap at the shipped default (" << kDefaultBlades << " blades): " << describe(w);
}

// The headline case: no voxel-local scheme can fix this one, because neither voxel can see the
// other. Roots clamped into [0.005,0.995] per voxel put neighbours 0.01u apart across the border.
TEST(GrassBladePacking, RootsNeverOverlapAcrossVoxelBorders) {
    const auto rs = rootsOverBlock(4, kDefaultBlades, 0.0f);
    std::vector<Root> pairsOnly;
    Worst w;
    // Restrict to pairs from DIFFERENT voxels by testing each voxel's roots against its neighbours.
    const float width = rs.empty() ? 0.0f : rs[0].width;
    for (size_t i = 0; i < rs.size(); ++i)
        for (size_t j = i + 1; j < rs.size(); ++j) {
            if (rs[i].vx == rs[j].vx && rs[i].vz == rs[j].vz) continue;
            if (std::abs(rs[i].vx - rs[j].vx) > 1 || std::abs(rs[i].vz - rs[j].vz) > 1) continue;
            const double dx = rs[i].x - rs[j].x, dz = rs[i].z - rs[j].z;
            const double d = std::sqrt(dx * dx + dz * dz);
            const double req = static_cast<double>(width);
            if (d - req < w.gap - w.required) w = {d, req, rs[i], rs[j], true};
        }
    ASSERT_TRUE(w.found);
    EXPECT_GT(w.gap, w.required)
        << "blades in ADJACENT VOXELS overlap — the per-voxel root clamp is the mechanism: "
        << describe(w);
}

TEST(GrassBladePacking, RootsNeverOverlapAcrossTheDistanceSweep) {
    // Width is distance-dependent (density compensation, then the sub-pixel floor), so the guarantee
    // has to hold along the whole radius, not just where the player stands.
    for (float d = 0.0f; d <= kRadius; d += 8.0f) {
        const auto rs = rootsOverBlock(3, kDefaultBlades, d);
        const Worst w = closestPair(rs);
        ASSERT_TRUE(w.found);
        EXPECT_GT(w.gap, w.required) << "overlap at camera distance " << d << ": " << describe(w);
    }
}

TEST(GrassBladePacking, RootsNeverOverlapAtTheCountsUnderConsideration) {
    for (uint32_t n : {1u, 7u, 20u, 30u, 56u, 90u, 140u}) {
        const auto rs = rootsOverBlock(3, n, 0.0f);
        if (rs.size() < 2) continue;
        const Worst w = closestPair(rs);
        // closestPair only inspects pairs within ~2 bucket widths. At very low counts every blade
        // is ~1u from its nearest neighbour, so NO pair is close enough to be worth inspecting —
        // that means trivially no overlap, not a missing result. (Asserting w.found here failed
        // spuriously at n=1 while the placement was genuinely fine.)
        if (!w.found) continue;
        EXPECT_GT(w.gap, w.required) << "overlap at " << n << " blades/voxel: " << describe(w);
    }
}

// ── The lattice's own properties (independent of any hash) ────────────────────────────────────

TEST(GrassBladePacking, SiteOrderIsABijection) {
    std::vector<int> seen(Phyxel::Graphics::kGrassSiteCount, 0);
    for (uint32_t i = 0; i < Phyxel::Graphics::kGrassSiteCount; ++i) {
        const uint8_t s = Phyxel::Graphics::kGrassSiteOrder[i];
        ASSERT_LT(s, Phyxel::Graphics::kGrassSiteCount);
        seen[s]++;
    }
    for (uint32_t s = 0; s < Phyxel::Graphics::kGrassSiteCount; ++s)
        EXPECT_EQ(seen[s], 1) << "site " << s << " used " << seen[s] << " times — not a bijection";
}

// The keystone property: EVERY prefix is well-spread, which is what lets the LOD thin the field by
// drawing fewer blades without the survivors clustering. If this fails, distant grass clumps.
TEST(GrassBladePacking, SiteOrderIsProgressive) {
    const uint32_t K = Phyxel::Graphics::kGrassGrid;
    const float pitch = Phyxel::Graphics::kGrassPitch;
    auto centre = [K, pitch](uint8_t s) {
        return glm::vec2((s % K) + 0.5f, (s / K) + 0.5f) * pitch;
    };
    // Toroidal: the per-voxel tile repeats every world unit, so the wrap-around distance is real.
    auto torusD = [](glm::vec2 a, glm::vec2 b) {
        float dx = std::fabs(a.x - b.x); dx = std::min(dx, 1.0f - dx);
        float dy = std::fabs(a.y - b.y); dy = std::min(dy, 1.0f - dy);
        return std::sqrt(dx * dx + dy * dy);
    };
    std::vector<glm::vec2> chosen;
    float running = 1e30f;
    for (uint32_t n = 1; n <= Phyxel::Graphics::kGrassSiteCount; ++n) {
        const glm::vec2 c = centre(Phyxel::Graphics::kGrassSiteOrder[n - 1]);
        for (const auto& p : chosen) running = std::min(running, torusD(c, p));
        chosen.push_back(c);
        if (n < 2) continue;
        const float envelope = Phyxel::Graphics::kGrassSeqSep / std::sqrt(static_cast<float>(n));
        EXPECT_GE(running, envelope - 1e-4f)
            << "prefix of " << n << " sites has min separation " << running
            << ", below the guaranteed envelope " << envelope
            << " — kGrassSeqSep is wrong for this table";
    }
}

// Guards the packing budget against a later "let's put the blade count back up" change.
TEST(GrassBladePacking, PackingBudgetHoldsAtShippedDefaults) {
    GrassRenderPipeline::Params p{};
    const float w = GrassRenderPipeline::bladeWidthAt(0.0f, p.radius, p.bladesPerVoxel,
                                                      p.bladeWidthScale, p.bladeStyle == 1);
    const float sep = Phyxel::Graphics::kGrassSeqSep
                    / std::sqrt(static_cast<float>(p.bladesPerVoxel));
    EXPECT_GT(sep, w)
        << "the shipped defaults do not fit: " << p.bladesPerVoxel << " blades gives "
        << sep << "u of guaranteed spacing but a blade is " << w
        << "u wide. Lower bladesPerVoxel or bladeWidthScale.";
}

TEST(GrassBladePacking, BladesPerVoxelCannotExceedTheGrid) {
    GrassRenderPipeline::Params p{};
    EXPECT_LE(p.bladesPerVoxel, Phyxel::Graphics::kGrassSiteCount)
        << "one blade per lattice cell: more blades than cells breaks exclusivity outright";
}
