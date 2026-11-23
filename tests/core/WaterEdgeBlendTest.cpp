// WaterCore.md 58.11 - the edge blend's rules (EdgeBlend.h; water_surface.vert's edgeBlendLift is its line-for-line copy).
#include <gtest/gtest.h>
#include "core/water/EdgeBlend.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace Phyxel::Core::Water;

namespace {
// a 40 m box at x 0..40, z 0..40 whose water stands at `inside` along every side; the record round it at `record`
EdgeProfile box(float inside) {
    EdgeProfile p; p.lo = glm::vec2(0.0f); p.hi = glm::vec2(40.0f);
    for (int s = 0; s < 4; ++s) { p.n[s] = 40; for (int m = 0; m < 40; ++m) p.tops[s][m] = inside; }
    return p;
}
}  // namespace

// The seam closes: the record drawn right at the box edge meets the volume's edge (the step 0), half a metre out it is mostly
// lifted, at M it is the record's own level again, and the drawn surface between is monotone (no new step). RED: M = 0 (off) -
// the full step remains. The three seams' sizes (58.4: 1 cm; 58.5: 12 cm; 58.9: 16 cm).
TEST(WaterEdgeBlend, TheRecordMeetsTheVolumesEdgeAndReturnsByM) {
    for (float step : {0.01f, 0.12f, 0.16f}) {
        const float record = 2.0f, inside = record + step;
        const std::vector<EdgeProfile> b{box(inside)};
        auto drawn = [&](float x, float M) { return record + edgeBlendLift(glm::vec3(x, record, 20.0f), b, M); };
        const float atEdge = drawn(40.0f + 1e-4f, 3.0f), atM = drawn(43.0f, 3.0f), off = drawn(40.0f + 1e-4f, 0.0f);
        float prev = atEdge, worstRise = 0.0f;
        for (float x = 40.05f; x <= 43.0f; x += 0.05f) { const float y = drawn(x, 3.0f); worstRise = std::max(worstRise, std::abs(y - prev)); prev = y; }
        std::printf("  step %.2f m: drawn at the box edge %.4f (the volume %.4f), at M %.4f (the record %.4f), largest jump over 5 cm %.4f | RED (off) at the edge %.4f\n",
                    step, atEdge, inside, atM, record, worstRise, off);
        EXPECT_NEAR(atEdge, inside, 1e-4f);
        EXPECT_NEAR(atM, record, 1e-5f);
        EXPECT_LT(worstRise, 0.15f * step + 1e-4f) << "the blend opened a step of its own";
        EXPECT_NEAR(off, record, 1e-6f) << "RED: off draws the record's level";
    }
}

// Inside the box nothing is lifted (the volume draws there); a dry side lifts nothing; the nearest box wins between two
TEST(WaterEdgeBlend, InsideDryAndTwoBoxes) {
    EdgeProfile a = box(2.1f), dry = box(2.1f);
    for (int m = 0; m < 40; ++m) dry.tops[3][m] = kEdgeDry;
    EXPECT_EQ(edgeBlendLift(glm::vec3(20.0f, 2.0f, 20.0f), {a}, 3.0f), 0.0f);
    EXPECT_EQ(edgeBlendLift(glm::vec3(41.0f, 2.0f, 20.0f), {dry}, 3.0f), 0.0f);
    EdgeProfile c = box(1.9f); c.lo = glm::vec2(44.0f, 0.0f); c.hi = glm::vec2(84.0f, 40.0f);
    const float nearA = edgeBlendLift(glm::vec3(40.5f, 2.0f, 20.0f), {a, c}, 3.0f), nearC = edgeBlendLift(glm::vec3(43.5f, 2.0f, 20.0f), {a, c}, 3.0f);
    EXPECT_GT(nearA, 0.0f); EXPECT_LT(nearC, 0.0f);
}
