#include <gtest/gtest.h>

#include "core/WorldGenerator.h"

#include <algorithm>
#include <cstdio>
#include <vector>

// L2 validation for the P0 mountain drama (docs/TerrainGenerationV2.md §P0). These assert the
// height field on the REAL generator output over a large region: dramatic relief (the old ~64
// cap is gone), mountains rougher/steeper than rolling hills, low ground stays near sea level,
// and NO cliff seams at chunk borders. Red baseline: the pre-P0 generator capped Mountains
// relief at ~64 voxels (grounding-auditor: below the 305 m mountain threshold) — these fail on it.

namespace Phyxel {
namespace {

constexpr int kSeaLevel = 16;

struct Field {
    int minY = 9999, maxY = -9999;
    std::vector<int> h;   // row-major over [x0,x1)×[z0,z1)
    int W = 0, D = 0, x0 = 0, z0 = 0;
    int at(int x, int z) const { return h[(z - z0) * W + (x - x0)]; }
};

Field sampleField(WorldGenerator& g, int x0, int z0, int W, int D) {
    Field f; f.W = W; f.D = D; f.x0 = x0; f.z0 = z0; f.h.resize(W * D);
    for (int z = 0; z < D; ++z)
        for (int x = 0; x < W; ++x) {
            int y = g.sampleSurface(x0 + x, z0 + z).surfaceY;
            f.h[z * W + x] = y;
            f.minY = std::min(f.minY, y);
            f.maxY = std::max(f.maxY, y);
        }
    return f;
}

// Fraction of interior columns whose 8×8 footprint relief (max-min surfaceY) exceeds `thresh`.
double steepFraction(const Field& f, int thresh) {
    const int win = 8;
    long steep = 0, total = 0;
    for (int z = f.z0; z + win < f.z0 + f.D; ++z)
        for (int x = f.x0; x + win < f.x0 + f.W; ++x) {
            int lo = 9999, hi = -9999;
            for (int dz = 0; dz < win; ++dz)
                for (int dx = 0; dx < win; ++dx) {
                    int v = f.at(x + dx, z + dz);
                    lo = std::min(lo, v); hi = std::max(hi, v);
                }
            if (hi - lo > thresh) ++steep;
            ++total;
        }
    return total ? static_cast<double>(steep) / total : 0.0;
}

TEST(TerrainReliefTest, MeasureAndValidateDrama) {
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 4242u);
    WorldGenerator perlin(WorldGenerator::GenerationType::Perlin, 4242u);
    WorldGenerator flat(WorldGenerator::GenerationType::Flat, 4242u);

    // Region large enough to cross several ridge wavelengths (~285 units) and many chunk borders.
    const int x0 = -256, z0 = -256, W = 512, D = 512;
    Field fm = sampleField(mtns, x0, z0, W, D);
    Field fp = sampleField(perlin, x0, z0, W, D);
    Field ff = sampleField(flat, x0, z0, W, D);

    double mSteep = steepFraction(fm, 6);
    double pSteep = steepFraction(fp, 6);
    std::printf("[relief] Mountains Y[%d,%d] peakRelief=%d steepFrac=%.3f\n",
                fm.minY, fm.maxY, fm.maxY - kSeaLevel, mSteep);
    std::printf("[relief] Perlin    Y[%d,%d] peakRelief=%d steepFrac=%.3f\n",
                fp.minY, fp.maxY, fp.maxY - kSeaLevel, pSteep);
    std::printf("[relief] Flat      Y[%d,%d]\n", ff.minY, ff.maxY);

    // Flat stays exactly at sea level.
    EXPECT_EQ(ff.minY, kSeaLevel);
    EXPECT_EQ(ff.maxY, kSeaLevel);

    // Dramatic peaks: grandest Mountains peak well past the old ~64 cap, approaching the ~384
    // target (with headroom for tuning). RED on pre-P0 (which capped at ~64).
    EXPECT_GT(fm.maxY - kSeaLevel, 250) << "mountains are not tall enough — drama missing";
    EXPECT_LT(fm.maxY - kSeaLevel, 460) << "mountains overshoot the ~384 compressed target";

    // Mountains are meaningfully rougher/steeper at building-footprint scale than rolling hills.
    EXPECT_GT(mSteep, pSteep + 0.05)
        << "mountains should have far more steep footprints than hills";

    // Low ground exists near/below sea level (oceans/lowlands), not a uniformly-raised plateau.
    EXPECT_LT(fm.minY, kSeaLevel + 24) << "no low ground — continental base never dips to coast";
}

TEST(TerrainReliefTest, NoCliffSeamsAtChunkBorders) {
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 99u);
    // Scan across several chunk borders (x multiples of 32). Natural terrain slope is bounded;
    // a masking/interp bug would show a sudden multi-voxel jump exactly at a border column.
    int worstJump = 0, worstX = 0;
    const int z = 40;
    for (int x = -200; x < 200; ++x) {
        int a = mtns.sampleSurface(x, z).surfaceY;
        int b = mtns.sampleSurface(x + 1, z).surfaceY;
        int jump = std::abs(a - b);
        if (jump > worstJump) { worstJump = jump; worstX = x; }
    }
    std::printf("[seam] worst adjacent-column jump = %d at x=%d\n", worstJump, worstX);
    // Steep mountains can legitimately climb several voxels per column, but a seam bug produces
    // a much larger discontinuity. Bound it well below a "cliff."
    EXPECT_LE(worstJump, 12) << "suspicious cliff — likely a chunk-border seam";
}

TEST(TerrainReliefTest, DeterministicAcrossInstances) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 7u);
    WorldGenerator b(WorldGenerator::GenerationType::Mountains, 7u);
    for (int i = 0; i < 200; ++i) {
        int x = (i * 37) - 500, z = (i * 53) - 300;
        EXPECT_EQ(a.sampleSurface(x, z).surfaceY, b.sampleSurface(x, z).surfaceY);
    }
}

}  // namespace
}  // namespace Phyxel
