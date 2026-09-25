// TierRanges: per-(face direction, voxel tier) instance ranges (docs/PerfProgram2026-09.md, I5/I6).
//
// A chunk's instance buffer is ordered direction-major, tier-minor. That is exactly the order the
// mesher already produced: it builds cube faces, then subcube, then microcube, and the direction
// sort is a STABLE counting sort. Recording tier sub-ranges therefore reorders nothing. The
// run builder lets a pass draw a subset of tiers (I6) and, with every tier on, must reproduce the
// existing direction-run behaviour.
#include <gtest/gtest.h>

#include "core/Types.h"
#include "graphics/TierRanges.h"

namespace TR = Phyxel::Graphics::TierRanges;
namespace IDU = Phyxel::InstanceDataUtils;

namespace {

// Offsets for a chunk with `n[d][t]` instances in each bucket.
TR::Offsets offsetsFrom(const uint32_t n[6][TR::kTiers]) {
    TR::Offsets off{};
    uint32_t run = 0;
    for (uint32_t d = 0; d < 6; ++d)
        for (uint32_t t = 0; t < TR::kTiers; ++t) {
            off[d * TR::kTiers + t] = run;
            run += n[d][t];
        }
    off[TR::kBuckets] = run;
    return off;
}

uint32_t total(const std::vector<TR::Run>& runs) {
    uint32_t s = 0;
    for (const auto& r : runs) s += r.count;
    return s;
}

}  // namespace

TEST(TierRangesTest, TierAndDirectionDecode) {
    const uint32_t cube = IDU::packCubeFaceDataSized(1, 2, 3, 4, 5, 6);   // +Y, 5x6
    EXPECT_EQ(TR::tierOf(cube), 0u);
    EXPECT_EQ(TR::dirOf(cube), 4u);
    EXPECT_EQ(TR::unitFaces(cube, 0u), 30u);
    // A fine face carries its extents in the light word.
    const uint32_t fineLight = IDU::packFineExtentsIntoLight(0u, 3, 7);
    const uint32_t micro = (1u) | (2u << 15) | (2u << 18);   // tier 2, +X
    EXPECT_EQ(TR::tierOf(micro), 2u);
    EXPECT_EQ(TR::dirOf(micro), 2u);
    EXPECT_EQ(TR::unitFaces(micro, fineLight), 21u);
    // Unmerged faces cover exactly one unit face.
    EXPECT_EQ(TR::unitFaces(IDU::packCubeFaceData(0, 0, 0, 1), 0u), 1u);
    EXPECT_EQ(TR::unitFaces(micro, 0u), 1u);
}

// All tiers on: the runs cover exactly the requested directions, as the old direction-run code did.
TEST(TierRangesTest, AllTiersReproducesDirectionRuns) {
    uint32_t n[6][TR::kTiers] = {{4, 2, 1, 0}, {3, 0, 0, 0}, {5, 5, 5, 0}, {0, 0, 0, 0}, {7, 1, 0, 0}, {2, 2, 2, 0}};
    const auto off = offsetsFrom(n);
    std::vector<TR::Run> runs;
    TR::buildRuns(off, 0x3Fu, TR::kAllTiers, runs);
    ASSERT_EQ(runs.size(), 1u);                    // everything is one contiguous run
    EXPECT_EQ(runs[0].first, 0u);
    EXPECT_EQ(runs[0].count, off[TR::kBuckets]);

    // Directions +Z and +X only (bits 0 and 2): two runs, exactly those directions' instances.
    TR::buildRuns(off, 0b000101u, TR::kAllTiers, runs);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].first, off[0 * TR::kTiers]);
    EXPECT_EQ(runs[0].count, 7u);
    EXPECT_EQ(runs[1].first, off[2 * TR::kTiers]);
    EXPECT_EQ(runs[1].count, 15u);
}

// Masking a tier removes exactly that tier's instances, direction by direction.
TEST(TierRangesTest, MaskingATierRemovesExactlyItsInstances) {
    uint32_t n[6][TR::kTiers] = {{4, 2, 1, 0}, {3, 0, 6, 0}, {5, 5, 5, 0}, {0, 1, 0, 0}, {7, 1, 0, 0}, {2, 2, 2, 0}};
    const auto off = offsetsFrom(n);
    std::vector<TR::Run> runs;
    // Drop microcubes (tier 2).
    TR::buildRuns(off, 0x3Fu, TR::kAllTiers & ~(1u << 2), runs);
    EXPECT_EQ(total(runs), off[TR::kBuckets] - (1 + 6 + 5 + 0 + 0 + 2));
    for (const auto& r : runs)
        for (uint32_t i = r.first; i < r.first + r.count; ++i)
            for (uint32_t d = 0; d < 6; ++d)   // no instance of a run lies in a micro bucket
                EXPECT_FALSE(i >= off[d * TR::kTiers + 2] && i < off[d * TR::kTiers + 3]);
    // Drop subcubes (tier 1): cube and micro runs cannot merge across it.
    TR::buildRuns(off, 0x3Fu, TR::kAllTiers & ~(1u << 1), runs);
    EXPECT_EQ(total(runs), off[TR::kBuckets] - (2 + 0 + 5 + 1 + 1 + 2));
}

// Everything masked: no runs at all.
TEST(TierRangesTest, NothingKeptGivesNoRuns) {
    uint32_t n[6][TR::kTiers] = {{4, 2, 1, 0}, {3, 0, 6, 0}, {5, 5, 5, 0}, {0, 1, 0, 0}, {7, 1, 0, 0}, {2, 2, 2, 0}};
    const auto off = offsetsFrom(n);
    std::vector<TR::Run> runs;
    TR::buildRuns(off, 0x3Fu, 0u, runs);
    EXPECT_TRUE(runs.empty());
    TR::buildRuns(off, 0u, TR::kAllTiers, runs);
    EXPECT_TRUE(runs.empty());
}
