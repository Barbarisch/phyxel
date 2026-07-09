#include <gtest/gtest.h>

#include "core/Spline.h"

#include <cmath>

namespace Phyxel {
namespace {

float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

TEST(SplineTest, EmptyEvaluatesToZero) {
    Spline s;
    EXPECT_TRUE(s.empty());
    EXPECT_FLOAT_EQ(s.eval(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(s.eval(0.5f), 0.0f);
}

TEST(SplineTest, SinglePointIsConstant) {
    Spline s({{0.3f, 7.0f}});
    EXPECT_FLOAT_EQ(s.eval(-100.0f), 7.0f);
    EXPECT_FLOAT_EQ(s.eval(0.3f), 7.0f);
    EXPECT_FLOAT_EQ(s.eval(100.0f), 7.0f);
}

TEST(SplineTest, ClampsOutsideTheRange) {
    Spline s = Spline::ramp(0.0f, -40.0f, 1.0f, 96.0f);
    EXPECT_FLOAT_EQ(s.eval(-1.0f), -40.0f);  // below first → first y
    EXPECT_FLOAT_EQ(s.eval(2.0f), 96.0f);    // above last  → last y
}

TEST(SplineTest, HitsControlPointsExactly) {
    Spline s({{0.0f, 0.0f}, {0.5f, 10.0f}, {1.0f, 4.0f}});
    EXPECT_NEAR(s.eval(0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(s.eval(0.5f), 10.0f, 1e-5f);
    EXPECT_NEAR(s.eval(1.0f), 4.0f, 1e-5f);
}

TEST(SplineTest, InterpolatesWithSmoothstepNotLinear) {
    // THE discriminating test: a [0,1]→[0,10] ramp at x=0.25.
    // smoothstep(0.25)=0.15625 → 1.5625; a LINEAR sampler would give 2.5. If this ever reads ~2.5,
    // the spline silently became linear and the "smooth, flat-at-knots" contract is broken.
    Spline s = Spline::ramp(0.0f, 0.0f, 1.0f, 10.0f);
    EXPECT_NEAR(s.eval(0.25f), 10.0f * smoothstep(0.25f), 1e-4f);
    EXPECT_GT(std::abs(s.eval(0.25f) - 2.5f), 0.5f) << "spline is behaving linearly, not smoothstep";
    EXPECT_NEAR(s.eval(0.5f), 5.0f, 1e-4f);  // midpoint is the average either way
}

TEST(SplineTest, RampReproducesSmoothstepMappedRange) {
    // ramp(0,a,1,b).eval(t) must equal a + (b-a)*smoothstep(t) — this is exactly the old hardcoded
    // continentalBase (seaLevel+min .. seaLevel+max via smoothstep), so wiring it is behavior-preserving.
    const float a = 16.0f - 40.0f, b = 16.0f + 96.0f;
    Spline s = Spline::ramp(0.0f, a, 1.0f, b);
    for (float t = 0.0f; t <= 1.0f; t += 0.1f)
        EXPECT_NEAR(s.eval(t), a + (b - a) * smoothstep(t), 1e-3f) << "at t=" << t;
}

TEST(SplineTest, MonotoneControlPointsNeverOvershoot) {
    Spline s({{0.0f, 0.0f}, {0.4f, 30.0f}, {0.7f, 30.0f}, {1.0f, 120.0f}});
    float prev = -1e9f;
    for (float t = 0.0f; t <= 1.0f; t += 0.02f) {
        float v = s.eval(t);
        EXPECT_GE(v, -1e-4f);          // never below the min control y (0)
        EXPECT_LE(v, 120.0f + 1e-4f);  // never above the max control y (120) — no overshoot
        EXPECT_GE(v, prev - 1e-4f);    // non-decreasing for non-decreasing control points
        prev = v;
    }
}

TEST(SplineTest, SortsUnsortedInput) {
    Spline s({{1.0f, 4.0f}, {0.0f, 0.0f}, {0.5f, 10.0f}});  // deliberately out of order
    EXPECT_NEAR(s.eval(0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(s.eval(0.5f), 10.0f, 1e-5f);
    EXPECT_NEAR(s.eval(1.0f), 4.0f, 1e-5f);
}

}  // namespace
}  // namespace Phyxel
