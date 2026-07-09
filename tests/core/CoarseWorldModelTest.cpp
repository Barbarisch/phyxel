#include <gtest/gtest.h>

#include "core/CoarseWorldModel.h"

#include <cmath>
#include <vector>

namespace Phyxel {
namespace {

// A source whose baseHeight == the world X of the queried corner, and continentalness ==
// world Z (scaled). Pure function of world position, so adjacent cells share corner values.
// A correct bilinear sampler reproduces the underlying linear field EXACTLY and is
// continuous across cell borders; a naive nearest/stepped sampler is not.
CoarseWorldModel::SourceFunc linearSource() {
    return [](float x, float z) {
        CoarseSample s;
        s.baseHeight = x;
        s.continentalness = z;
        s.temperature = 0.25f;   // constant fields must pass straight through
        s.moisture = 0.75f;
        return s;
    };
}

TEST(CoarseWorldModelTest, ReturnsSourceExactlyAtCorners) {
    CoarseWorldModel m(linearSource(), 32.0f);
    for (float x : {-64.0f, 0.0f, 32.0f, 96.0f}) {
        for (float z : {-32.0f, 0.0f, 64.0f}) {
            CoarseSample s = m.sample(x, z);
            EXPECT_NEAR(s.baseHeight, x, 1e-3f) << "corner (" << x << "," << z << ")";
            EXPECT_NEAR(s.continentalness, z, 1e-3f);
        }
    }
}

TEST(CoarseWorldModelTest, ReproducesLinearFieldBetweenCorners) {
    // For a linear underlying field, bilinear interpolation must reproduce it everywhere,
    // not just at corners. Sample at arbitrary sub-cell positions.
    CoarseWorldModel m(linearSource(), 32.0f);
    for (float x = -50.0f; x <= 100.0f; x += 3.7f) {
        for (float z = -20.0f; z <= 80.0f; z += 5.3f) {
            CoarseSample s = m.sample(x, z);
            EXPECT_NEAR(s.baseHeight, x, 1e-2f) << "at (" << x << "," << z << ")";
            EXPECT_NEAR(s.continentalness, z, 1e-2f);
        }
    }
}

TEST(CoarseWorldModelTest, ConstantFieldsPassThrough) {
    CoarseWorldModel m(linearSource(), 40.0f);
    CoarseSample s = m.sample(13.0f, 27.0f);
    EXPECT_NEAR(s.temperature, 0.25f, 1e-4f);
    EXPECT_NEAR(s.moisture, 0.75f, 1e-4f);
}

TEST(CoarseWorldModelTest, IsContinuousAcrossCellBorders) {
    // THE seam test: crossing a cell boundary must not jump. A nearest/stepped sampler
    // would cliff by ~cellSize here; bilinear changes by ~the step we took.
    const float cs = 32.0f;
    CoarseWorldModel m(linearSource(), cs);
    for (float border : {-32.0f, 0.0f, 32.0f, 64.0f}) {
        CoarseSample lo = m.sample(border - 0.01f, 5.0f);
        CoarseSample hi = m.sample(border + 0.01f, 5.0f);
        EXPECT_NEAR(lo.baseHeight, hi.baseHeight, 0.05f)
            << "discontinuity crossing x=" << border;
    }
}

TEST(CoarseWorldModelTest, IsDeterministicAndOrderIndependent) {
    CoarseWorldModel m(linearSource(), 32.0f);
    struct P { float x, z; };
    std::vector<P> pts = {{7, 3}, {40, 40}, {-15, 22}, {100, -8}, {33, 65}};
    std::vector<CoarseSample> first;
    for (const auto& p : pts) first.push_back(m.sample(p.x, p.z));
    // Re-sample in reverse order: results must be identical (no evaluation-order state).
    for (int i = static_cast<int>(pts.size()) - 1; i >= 0; --i) {
        CoarseSample s = m.sample(pts[i].x, pts[i].z);
        EXPECT_FLOAT_EQ(s.baseHeight, first[i].baseHeight);
        EXPECT_FLOAT_EQ(s.continentalness, first[i].continentalness);
    }
}

// Falsifiability guard (red-before-green): prove the continuity + midpoint invariants above
// actually DISCRIMINATE — i.e. the naive "snap to cell corner, no interpolation" sampler that
// CoarseWorldModel replaces FAILS them. If these EXPECT_GT/EXPECT_NEAR ever flip, the invariant
// tests have gone vacuous. This documents WHY bilinear interpolation is required, not decorative.
TEST(CoarseWorldModelTest, NaiveNearestSamplerFailsTheInvariants_soTheyAreMeaningful) {
    const float cs = 32.0f;
    auto src = linearSource();  // baseHeight == world X
    // Nearest/stepped sampler: snap to the low corner, return it verbatim (no interpolation).
    auto nearest = [&](float x, float /*z*/) { return src(std::floor(x / cs) * cs, 0.0f).baseHeight; };

    // (a) It cliffs at a cell border by ~cellSize, where bilinear changes by ~the step taken.
    const float border = 32.0f;
    float jump = std::abs(nearest(border + 0.01f, 5.0f) - nearest(border - 0.01f, 5.0f));
    EXPECT_GT(jump, cs * 0.5f) << "naive sampler should cliff; if not, the continuity test is vacuous";

    // (b) At a cell midpoint it is off the true linear value by ~half a cell.
    float mid = cs * 0.5f;
    float err = std::abs(nearest(mid, 0.0f) - mid);
    EXPECT_GT(err, cs * 0.25f) << "naive sampler should miss the midpoint; if not, the midpoint test is vacuous";

    // And the real model passes exactly where the naive one fails (contrast, same inputs).
    CoarseWorldModel m(src, cs);
    EXPECT_NEAR(m.sample(border + 0.01f, 5.0f).baseHeight,
                m.sample(border - 0.01f, 5.0f).baseHeight, 0.05f);
    EXPECT_NEAR(m.sample(mid, 0.0f).baseHeight, mid, 1e-2f);
}

TEST(CoarseWorldModelTest, BilinearMidpointAveragesFourCorners) {
    // Source = x+z; the four corners of cell [0,cs]x[0,cs] are 0, cs, cs, 2cs; the centre
    // must be the mean (cs). Guards the bilinear weights specifically.
    const float cs = 32.0f;
    CoarseWorldModel m([](float x, float z) { CoarseSample s; s.baseHeight = x + z; return s; }, cs);
    CoarseSample c = m.sample(cs * 0.5f, cs * 0.5f);
    EXPECT_NEAR(c.baseHeight, cs, 1e-2f);
}

}  // namespace
}  // namespace Phyxel
