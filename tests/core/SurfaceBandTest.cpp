#include <gtest/gtest.h>

#include <algorithm>
#include <climits>
#include <cstdio>

#include "core/WorldGenerator.h"

using namespace Phyxel;

// ============================================================================
// SurfaceBandTest: M1d (docs/WorldForgeV2Plan.md section 5 M1 resolution 9; audit section 5's
// "latent mountain-flank hole"). The streaming manager wants the chunks around each column's
// TERRAIN SURFACE band +-1. It used to ask ONE sample at the chunk column's centre, so a flank
// whose surface spans more bands than the centre +-1 never streamed its upper or lower chunks.
// Invariant: for every chunk column, every one of its 1024 columns' surface band lies inside
// [lo - 1, hi + 1] of what surfaceBandForChunkColumn() reports (the +-1 is the manager's pad).
// Rig: Mountains seed 424242 (the plan's stress seed), 120 chunk columns on a scattered
// lattice over +-6 km; the prediction written first: the centre-only answer MISSES on flanks,
// a corners+centre answer does not (if it still misses, widen the sample set, never the pad).
// ============================================================================
namespace {
int bandOf(int y) { return y >= 0 ? y / 32 : (y - 31) / 32; }
}  // namespace

TEST(SurfaceBandTest, EveryColumnOfEveryChunkColumnLiesInsideTheReportedBand) {
    WorldGenerator gen(WorldGenerator::GenerationType::Mountains, 424242u);
    (void)gen.sampleSurface(0, 0);   // one-time bakes outside the measurement
    int chunkColumns = 0, missingColumns = 0, chunkColumnsWithAMiss = 0;
    int worstSpan = 0, firstCx = 0, firstCz = 0, spanSum = 0;
    for (int i = 0; i < 120; ++i) {
        const int cx = ((i * 7919) % 375) - 187;   // chunk coords, +-6 km
        const int cz = ((i * 104729) % 377) - 188;
        const glm::ivec2 band = gen.surfaceBandForChunkColumn(cx, cz);
        ASSERT_NE(band.x, INT_MIN);
        int lo = INT_MAX, hi = INT_MIN, misses = 0;
        for (int lx = 0; lx < 32; ++lx)
            for (int lz = 0; lz < 32; ++lz) {
                const int b = bandOf(gen.sampleSurface(cx * 32 + lx, cz * 32 + lz).surfaceY);
                lo = std::min(lo, b);
                hi = std::max(hi, b);
                if (b < band.x - 1 || b > band.y + 1) ++misses;
            }
        ++chunkColumns;
        worstSpan = std::max(worstSpan, hi - lo);
        spanSum += (band.y - band.x);
        if (misses > 0) {
            if (chunkColumnsWithAMiss == 0) { firstCx = cx; firstCz = cz; }
            ++chunkColumnsWithAMiss;
            missingColumns += misses;
        }
    }
    std::printf("[ M1d ] %d chunk columns, worst surface span %d bands, %d chunk columns with a miss, %d columns outside the band; "
                "reported band spread mean %.2f bands (each column streams spread+3 chunks: cost, unmeasured at L4)\n",
                chunkColumns, worstSpan, chunkColumnsWithAMiss, missingColumns,
                static_cast<double>(spanSum) / std::max(1, chunkColumns));
    EXPECT_EQ(chunkColumnsWithAMiss, 0)
        << chunkColumnsWithAMiss << " of " << chunkColumns << " chunk columns have surface columns outside "
        << "the reported band +-1 (" << missingColumns << " columns; first at chunk (" << firstCx << ","
        << firstCz << ")): those chunks never stream from the surface band";
    // Control: the rig has real multi-band flanks (a flat world would pass trivially).
    EXPECT_GE(worstSpan, 2) << "no chunk column spans more than one band: the rig has no flank";
}
