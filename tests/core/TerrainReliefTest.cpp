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

TEST(TerrainReliefTest, MountainsSlopeGraduallyNotSheerColumns) {
    WorldGenerator mtns(WorldGenerator::GenerationType::Mountains, 99u);
    // "Sloped flank vs sheer column" is a WORST-STEP property, not an average: both versions slope
    // gently on average (most steps <1 voxel), but the pre-fix single-band relief (kRmFreq=0.006 ×
    // amp 288) puts occasional sharp ridge-cusp cliffs of ~15 voxels in a single horizontal step —
    // the "columns rising well above the terrain" the player saw. The broad+fine fix spreads the
    // massif height over a ~4.6× longer wavelength, so the amplitude that reaches any single cusp is
    // the small fine band only → the worst step drops to ~3. We scan the FULL mountain body (2D, both
    // x- and z-neighbours; one scan-line undersamples the true worst) for the steepest single step.
    // Measured: pre-fix worst = 15 (FAILS), fixed worst = 3 (PASSES) — this straddles the threshold.
    int worst = 0, wx = 0, wz = 0;
    long steps = 0;
    for (int z = -220; z <= 220; z += 2)
        for (int x = -220; x < 220; ++x) {
            int a = mtns.sampleSurface(x, z).surfaceY;
            if (a <= kSeaLevel + 120) continue;   // only the mountain body, not lowland/valley
            int jx = std::abs(a - mtns.sampleSurface(x + 1, z).surfaceY);
            int jz = std::abs(a - mtns.sampleSurface(x, z + 1).surfaceY);
            int j = std::max(jx, jz);
            if (j > worst) { worst = j; wx = x; wz = z; }
            ++steps;
        }
    ASSERT_GT(steps, 500) << "too few mountain columns sampled to judge slope";
    std::printf("[slope] worst single step over %ld mountain columns = %d at (%d,%d)\n",
                steps, worst, wx, wz);
    EXPECT_LE(worst, 6) << "a single column climbs >6 voxels in one step — a sheer cliff/tower, not a sloped flank";
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
