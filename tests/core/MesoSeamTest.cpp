#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <thread>

#include "core/WorldSheet.h"

using namespace Phyxel;

// ============================================================================
// MesoSeamTest: docs/WorldForgeV2Plan.md section 5 M1 resolution 2 (FeatureDesignKeys: "chunking
// must not change the answer"). Tiles bound COST only: a tile baked alone must equal the same
// cells baked as part of a whole-region window, BIT-EXACTLY, and bicubic samples straddling a
// tile edge must equal the whole-region sample. The macro/detail inputs are pure functions of
// world position (any will do; the invariant is about the tile pipeline).
// ============================================================================
namespace {

float macroField(float x, float z) {
    return 40.0f + 0.01f * x + 20.0f * std::sin(z * (1.0f / 700.0f)) +
           12.0f * std::cos((x + z) * (1.0f / 1300.0f));
}

// Position-only hash noise at the cell centre: rough enough that thermal erosion fires.
float detailField(float x, float z) {
    uint64_t h = static_cast<uint64_t>(static_cast<int64_t>(std::floor(x))) * 0x9E3779B97F4A7C15ull ^
                 static_cast<uint64_t>(static_cast<int64_t>(std::floor(z))) * 0xC2B2AE3D27D4EB4Full;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ull;
    h ^= h >> 32;
    return (static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0xFFFFFF) - 0.5f) * 60.0f;
}

SheetParams labParams(int steps) {
    SheetParams p;
    p.mesoCellU = 32.0f;
    p.mesoTileCells = 64;   // smaller than the shipped 256 so four tiles + a 128^2 window bake in ms;
                            // the invariant does not depend on N (stated rig delta)
    p.mesoSteps = steps;
    return p;
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

}  // namespace

TEST(MesoSeamTest, ApronIsDerivedFromTheStepCount) {
    for (int steps : {0, 1, 4, 9}) {
        const SheetParams p = SheetParams::clamped(labParams(steps));
        EXPECT_EQ(p.apronCells(), steps * kMesoStencilRadius + kMesoBicubicReach) << "steps=" << steps;
    }
    SheetParams over = labParams(99);
    EXPECT_EQ(SheetParams::clamped(over).mesoSteps, 32) << "steps must be clamped (apron growth)";
    SheetParams hot = labParams(1);
    hot.diffusion = 0.9f;
    EXPECT_LT(SheetParams::clamped(hot).diffusion, 0.25f) << "explicit diffusion must stay stable";
}

// The invariant: four tiles baked independently, in random order, on four threads == one
// 2N x 2N window baked at once, every interior cell, bit for bit.
TEST(MesoSeamTest, TileByTileEqualsWholeRegionBitExact) {
    const SheetParams p = labParams(4);
    MesoBaker baker(7001u, p, macroField, detailField);
    const int N = baker.params().mesoTileCells;

    std::vector<std::pair<int, int>> order = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    std::shuffle(order.begin(), order.end(), std::mt19937(20261015u));
    std::shared_ptr<const MesoTile> tiles[2][2];
    std::vector<std::thread> threads;
    for (auto [tx, tz] : order)
        threads.emplace_back([&, tx, tz] { tiles[tx][tz] = baker.bake(tx, tz); });
    for (auto& th : threads) th.join();

    const auto whole = baker.bakeCells(0, 0, 2 * N);

    size_t mismatches = 0;
    int firstI = -1, firstJ = -1, firstTx = -1, firstTz = -1;
    float firstTile = 0.0f, firstWhole = 0.0f;
    for (int tx = 0; tx < 2; ++tx)
        for (int tz = 0; tz < 2; ++tz)
            for (int j = 0; j < N; ++j)
                for (int i = 0; i < N; ++i) {
                    const float a = tiles[tx][tz]->at(i, j);
                    const float b = whole->at(tx * N + i, tz * N + j);
                    if (!sameBits(a, b)) {
                        if (mismatches == 0) {
                            firstI = i; firstJ = j; firstTx = tx; firstTz = tz;
                            firstTile = a; firstWhole = b;
                        }
                        ++mismatches;
                    }
                }
    EXPECT_EQ(mismatches, 0u)
        << mismatches << " of " << 4 * N * N << " interior cells differ between tile-by-tile and "
        << "whole-region bakes; first at tile (" << firstTx << "," << firstTz << ") cell (" << firstI
        << "," << firstJ << "): tile=" << firstTile << " whole=" << firstWhole
        << " (diff " << (firstTile - firstWhole) << "): the tile's dependency cone leaks past its apron";
}

// Samples straddling the seam at x = N*cellU, served by the pager from two different tiles,
// must equal the whole-region window's samples bit for bit (the bicubic reads the apron).
TEST(MesoSeamTest, BicubicSamplesStraddlingTheSeamMatchTheWholeRegion) {
    const SheetParams p = labParams(4);
    auto baker = std::make_shared<MesoBaker>(7001u, p, macroField, detailField);
    const int N = baker->params().mesoTileCells;
    const float seamX = N * p.mesoCellU;
    MesoPager pager(baker, 8);
    const auto whole = baker->bakeCells(0, 0, 2 * N);

    size_t mismatches = 0, samples = 0;
    float worst = 0.0f;
    for (float wz : {5.0f, 333.3f, 1000.0f, 1990.0f})
        for (float dx = -70.0f; dx <= 70.0f; dx += 3.7f) {
            const float wx = seamX + dx;
            const float a = pager.heightAt(wx, wz);
            const float b = mesoHeightAt(*whole, wx, wz);
            ++samples;
            if (!sameBits(a, b)) {
                ++mismatches;
                worst = std::max(worst, std::fabs(a - b));
            }
        }
    EXPECT_EQ(mismatches, 0u) << mismatches << " of " << samples
                              << " seam-straddling samples differ (worst " << worst << " u)";
    EXPECT_GE(pager.bakes(), 2u) << "the pager did not serve the seam from two tiles";
}

// The climate planes and the analytic gradient must obey the same seam invariant as the height
// (auditor finding 2026-10-15: the generator-copy test compares one pager against itself and
// cannot see a tile-vs-whole discrepancy). Independent pagers: tiles via pager->get against
// one 2N x 2N window, for climateAt and gradientAt at x = seam +- 20 u.
TEST(MesoSeamTest, ClimateAndGradientStraddlingTheSeamMatchTheWholeRegion) {
    auto climate = [](float x, float z, float& t, float& m) {
        t = detailField(x * 0.37f, z * 0.91f) * 0.01f;
        m = detailField(x * 1.13f + 7.0f, z * 0.53f) * 0.01f;
    };
    const SheetParams p = labParams(4);
    auto baker = std::make_shared<MesoBaker>(7001u, p, macroField, detailField, climate);
    const int N = baker->params().mesoTileCells;
    const float seamX = N * p.mesoCellU;
    MesoPager pager(baker, 8);
    const auto whole = baker->bakeCells(0, 0, 2 * N);

    size_t mismatches = 0, samples = 0;
    for (float wz : {12.0f, 700.0f, 1500.0f})
        for (float dx = -20.0f; dx <= 20.0f; dx += 1.9f) {
            const float wx = seamX + dx;
            float ta, ma, tb, mb;
            pager.climateAt(wx, wz, ta, ma);
            mesoClimateAt(*whole, wx, wz, tb, mb);
            const auto ga = pager.gradientAt(wx, wz);
            const auto gb = mesoGradientAt(*whole, wx, wz);
            ++samples;
            if (!sameBits(ta, tb) || !sameBits(ma, mb) || !sameBits(ga.first, gb.first) ||
                !sameBits(ga.second, gb.second))
                ++mismatches;
        }
    EXPECT_EQ(mismatches, 0u) << mismatches << " of " << samples
                              << " climate/gradient samples differ across the seam";
    EXPECT_GE(pager.bakes(), 2u);
    // Control: the climate planes carry real content (a flat plane would pass trivially).
    float t0, m0, t1, m1;
    pager.climateAt(seamX - 300.0f, 12.0f, t0, m0);
    pager.climateAt(seamX + 300.0f, 700.0f, t1, m1);
    EXPECT_FALSE(sameBits(t0, t1) && sameBits(m0, m1)) << "the climate planes are constant";
}

// Eviction + re-bake is bit-identical (the bake is pure), and the LRU honours its cap.
TEST(MesoSeamTest, PagerEvictsAndRebakesIdentically) {
    auto baker = std::make_shared<MesoBaker>(7001u, labParams(2), macroField, detailField);
    MesoPager pager(baker, 2);
    const auto first = pager.get(0, 0);
    const std::vector<float> snapshot = first->raw();
    (void)pager.get(1, 0);
    (void)pager.get(2, 0);   // evicts (0,0)
    EXPECT_EQ(pager.resident(), 2u);
    const auto again = pager.get(0, 0);
    EXPECT_EQ(pager.bakes(), 4u) << "(0,0) was not evicted and re-baked";
    EXPECT_TRUE(again->raw() == snapshot) << "the re-baked tile differs from the evicted one";
}
