#include <gtest/gtest.h>

#include "core/FinePonds.h"
#include "core/WorldGenerator.h"

#include <cmath>

// Tangible-water Phase B: fine-scale pond discovery. The pure discovery function is tested on
// hand-crafted height windows (bowls); the WorldGenerator registry is pinned for determinism.

namespace Phyxel {
namespace {

// A flat plateau at 50 with a rectangular bowl (floor `floorH`) drained by an outlet CHANNEL at
// `spillH` running west from the bowl to the window border along row `chanZ`. The channel is a
// real escape path, so Priority-Flood fills the bowl to exactly spillH; the channel itself sits
// AT its fill level → dry. (A lone "notch" cell doesn't work: with plateau on every side of it,
// the cheapest path out still crosses the plateau and the bowl spills at 50.)
std::function<float(int, int)> bowlWindow(int x0, int x1, int z0, int z1, float floorH,
                                          float spillH, int chanZ) {
    return [=](int x, int z) -> float {
        if (z == chanZ && x < x0) return spillH;                           // outlet channel → border
        if (x >= x0 && x <= x1 && z >= z0 && z <= z1) return floorH;       // bowl interior
        return 50.0f;                                                       // plateau
    };
}

}  // namespace

TEST(FinePondTest, DiscoversABowlAtItsSpillMinusFreeboard) {
    // 4x3 bowl (12 columns) at height 10, drained west by a channel at 14.
    const auto h = bowlWindow(10, 13, 10, 12, 10.0f, 14.0f, 11);
    const auto ponds = discoverFinePonds(h, 32, 32);
    ASSERT_EQ(ponds.size(), 1u);
    const FinePond& p = ponds[0];
    EXPECT_EQ(static_cast<int>(p.columns.size()), 12);
    EXPECT_NEAR(p.level, 14.0f - kPondFreeboard, 1e-3f)
        << "pond must fill to its spill minus the freeboard (cannot leak by construction)";
    EXPECT_NEAR(p.depth, 4.0f, 1e-3f);
    EXPECT_EQ(p.bboxMin, glm::ivec2(10, 10));
    EXPECT_EQ(p.bboxMax, glm::ivec2(13, 12));
}

TEST(FinePondTest, RejectsBorderTouchingShallowTinyAndHugeBasins) {
    // Border-touching: bowl reaching x=0 → its basin escapes the window.
    EXPECT_TRUE(discoverFinePonds(bowlWindow(0, 4, 10, 12, 10.0f, 14.0f, 11), 32, 32).empty())
        << "a basin touching the window border must be discarded (seam-freedom rule)";
    // Too shallow: depth 1.0 < kPondMinDepth.
    EXPECT_TRUE(discoverFinePonds(bowlWindow(10, 13, 10, 12, 13.0f, 14.0f, 11), 32, 32).empty());
    // Too small: 2 columns < kPondMinArea.
    EXPECT_TRUE(discoverFinePonds(bowlWindow(10, 11, 10, 10, 10.0f, 14.0f, 10), 32, 32).empty());
    // Too big: a 15x15 = 225-column basin > kPondMaxArea.
    EXPECT_TRUE(discoverFinePonds(bowlWindow(8, 22, 8, 22, 10.0f, 14.0f, 15), 32, 32).empty());
}

TEST(FinePondTest, NestedSubBasinSharesTheParentSpill) {
    // A bowl whose floor has a deeper pocket: ONE pond at the outer spill, pocket included.
    auto h = [](int x, int z) -> float {
        if (z == 11 && x < 10) return 14.0f;                              // outlet channel
        if (x == 11 && z == 11) return 6.0f;                              // deep pocket
        if (x >= 10 && x <= 13 && z >= 10 && z <= 12) return 10.0f;       // bowl
        return 50.0f;
    };
    const auto ponds = discoverFinePonds(h, 32, 32);
    ASSERT_EQ(ponds.size(), 1u);
    EXPECT_EQ(static_cast<int>(ponds[0].columns.size()), 12);
    EXPECT_NEAR(ponds[0].level, 14.0f - kPondFreeboard, 1e-3f);
    EXPECT_NEAR(ponds[0].depth, 8.0f, 1e-3f) << "depth measures to the pocket floor";
    EXPECT_EQ(ponds[0].deepest, glm::ivec2(11, 11));
}

TEST(FinePondTest, TwoSeparateBowlsAreTwoPonds) {
    auto h = [](int x, int z) -> float {
        if (z == 7 && x < 6) return 14.0f;                           // A's outlet
        if (z == 21 && x < 20) return 16.0f;                         // B's outlet
        if (x >= 6 && x <= 8 && z >= 6 && z <= 8) return 10.0f;      // bowl A (9 cols)
        if (x >= 20 && x <= 22 && z >= 20 && z <= 22) return 10.0f;  // bowl B (9 cols)
        return 50.0f;
    };
    const auto ponds = discoverFinePonds(h, 32, 32);
    ASSERT_EQ(ponds.size(), 2u);
    EXPECT_NE(ponds[0].level, ponds[1].level) << "each bowl fills to its OWN outlet height";
}

// Registry determinism + rejection wiring against the REAL generator (seed 7 Mountains): two
// independent generators agree exactly; pond ids are stable; a returned hit round-trips through
// finePondAt membership.
TEST(FinePondTest, GeneratorRegistryIsDeterministicAndSelfConsistent) {
    WorldGenerator a(WorldGenerator::GenerationType::Mountains, 7u);
    WorldGenerator b(WorldGenerator::GenerationType::Mountains, 7u);

    int found = 0;
    for (int cz = -3; cz <= 3 && found < 3; ++cz)
        for (int cx = -3; cx <= 3 && found < 3; ++cx) {
            const auto pa = a.finePondsForCell(cx, cz);
            const auto pb = b.finePondsForCell(cx, cz);
            ASSERT_EQ(pa->size(), pb->size()) << "generators disagree at cell " << cx << "," << cz;
            for (size_t i = 0; i < pa->size(); ++i) {
                ++found;
                EXPECT_EQ((*pa)[i].id, (*pb)[i].id);
                EXPECT_FLOAT_EQ((*pa)[i].level, (*pb)[i].level);
                ASSERT_EQ((*pa)[i].columns, (*pb)[i].columns);
                // Membership round trip through the public query.
                const uint64_t c0 = (*pa)[i].columns.front();
                const int wx = static_cast<int32_t>(static_cast<uint32_t>(c0 >> 32));
                const int wz = static_cast<int32_t>(static_cast<uint32_t>(c0 & 0xffffffffu));
                const auto hit = a.finePondAt(wx, wz);
                EXPECT_EQ(hit.id, (*pa)[i].id) << "finePondAt missed a member column";
                EXPECT_FLOAT_EQ(hit.level, (*pa)[i].level);
            }
        }
    std::printf("[fineponds] seed7 Mountains: %d ponds across the scanned cells\n", found);
}

}  // namespace Phyxel
