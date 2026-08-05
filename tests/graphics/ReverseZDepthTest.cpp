// REVERSE-Z depth convention (engine/include/graphics/DepthConvention.h).
//
// The claim being made is precision + an infinite far plane. A test that only asserts "the matrix
// has these numbers in it" would restate the implementation, so these tests assert the PROPERTIES
// that motivated the change, and several of them are written to FAIL against the forward-Z setup
// that was replaced:
//
//   - depth is monotone and lands exactly on 1 at the near plane, approaching 0 at infinity;
//   - nothing is ever clipped for being too far away (that is what "no render distance" means);
//   - the OLD glm::perspective path threw away everything between near and ~2x near, because it
//     emits OpenGL [-1,1] clip depth into a Vulkan [0,1] pipeline (no GLM_FORCE_DEPTH_ZERO_TO_ONE
//     in this build). ForwardZClipsGeometryNearTheLens pins that defect so the regression is
//     visible if anyone reverts;
//   - float depth RESOLVES better under reverse-Z at distance — the actual justification.

#include <gtest/gtest.h>
#include "graphics/DepthConvention.h"
#include "graphics/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

using Phyxel::Graphics::Camera;
namespace DC = Phyxel::Graphics::DepthConvention;

namespace {

constexpr float kNear   = 0.1f;
constexpr float kAspect = 16.0f / 9.0f;
const     float kFovY   = glm::radians(45.0f);

// Depth in [0,1] as the rasteriser would see it, for a point `dist` in front of the camera.
float ndcDepthReverseZ(float dist) {
    const glm::mat4 p = DC::infiniteReverseZPerspective(kFovY, kAspect, kNear);
    const glm::vec4 clip = p * glm::vec4(0.0f, 0.0f, -dist, 1.0f);   // RH: -Z is forward
    return clip.z / clip.w;
}

// The projection this replaced: glm::perspective, which without GLM_FORCE_DEPTH_ZERO_TO_ONE is
// the OpenGL [-1,1] form. Returns clip-space z and w so clipping can be judged.
glm::vec2 clipForwardZ(float dist, float farP) {
    glm::mat4 p = glm::perspective(kFovY, kAspect, kNear, farP);
    p[1][1] *= -1.0f;
    const glm::vec4 clip = p * glm::vec4(0.0f, 0.0f, -dist, 1.0f);
    return glm::vec2(clip.z, clip.w);
}

} // namespace

TEST(ReverseZDepth, NearPlaneIsOneAndDistanceApproachesZero) {
    EXPECT_NEAR(ndcDepthReverseZ(kNear), 1.0f, 1e-5f);
    EXPECT_LT(ndcDepthReverseZ(1000.0f), 0.001f);
    EXPECT_LT(ndcDepthReverseZ(1.0e6f), 1.0e-6f);
    EXPECT_GT(ndcDepthReverseZ(1.0e6f), 0.0f) << "must approach zero, never reach or cross it";
}

TEST(ReverseZDepth, DepthDecreasesMonotonicallyWithDistance) {
    // Monotonicity is what makes a GREATER depth test mean "nearer wins". Swept, not sampled at
    // two points, because a sign error in one term can preserve the endpoints.
    float prev = ndcDepthReverseZ(kNear);
    for (float d = kNear; d < 5000.0f; d *= 1.05f) {
        const float z = ndcDepthReverseZ(d);
        ASSERT_LE(z, prev) << "depth increased with distance at d=" << d;
        ASSERT_GE(z, 0.0f) << "depth left [0,1] at d=" << d;
        ASSERT_LE(z, 1.0f + 1e-5f) << "depth left [0,1] at d=" << d;
        prev = z;
    }
}

TEST(ReverseZDepth, NothingIsEverClippedForBeingTooFar) {
    // THE POINT OF THE CHANGE. Vulkan clips unless 0 <= z_clip <= w_clip. Assert it holds at
    // distances far past any far plane the engine ever configured (4096), out to a million units.
    const glm::mat4 p = DC::infiniteReverseZPerspective(kFovY, kAspect, kNear);
    for (float d : {1.0f, 100.0f, 4096.0f, 50000.0f, 1.0e6f}) {
        const glm::vec4 clip = p * glm::vec4(0.0f, 0.0f, -d, 1.0f);
        ASSERT_GT(clip.w, 0.0f) << "w must stay positive at d=" << d;
        ASSERT_GE(clip.z, 0.0f) << "clipped as too far at d=" << d;
        ASSERT_LE(clip.z, clip.w) << "clipped as too near at d=" << d;
    }
}

TEST(ReverseZDepth, ForwardZClipsGeometryNearTheLens) {
    // The defect reverse-Z removed, pinned so a revert is visible. glm::perspective is the OpenGL
    // [-1,1] form here, so z_clip is NEGATIVE between the near plane and ~2x near — and Vulkan
    // clips z_clip < 0. That sliver at the lens was being silently discarded.
    const glm::vec2 atNear = clipForwardZ(kNear, 4096.0f);
    EXPECT_LT(atNear.x, 0.0f) << "expected the OpenGL [-1,1] form (z=-w at the near plane)";

    const glm::vec2 justPast = clipForwardZ(kNear * 1.5f, 4096.0f);
    EXPECT_LT(justPast.x, 0.0f) << "forward-Z should still be clipped at 1.5x near";

    // Reverse-Z keeps that same region.
    EXPECT_GE(ndcDepthReverseZ(kNear * 1.5f), 0.0f);
    EXPECT_LE(ndcDepthReverseZ(kNear * 1.5f), 1.0f);
}

TEST(ReverseZDepth, ResolvesDistantGeometryBetterThanForwardZ) {
    // The precision claim, measured rather than asserted: how far apart must two surfaces be at
    // 1 km before a 32-bit float depth buffer can tell them apart? Fewer distinguishable steps =
    // more z-fighting. Reverse-Z should win by orders of magnitude.
    // The starting step MUST be far below the expected answer or this measures nothing but its own
    // floor — a first draft started at 1e-4 and reported both conventions as "equal" at every
    // distance, which is how a real 11,000x gap can hide behind a passing test.
    auto distinguishable = [](float dist, auto depthFn) {
        const float d0 = depthFn(dist);
        float sep = 1.0e-7f;
        for (int i = 0; i < 500; ++i) {
            if (depthFn(dist + sep) != d0) return sep;   // exact float compare is the point
            sep *= 1.1f;
        }
        return sep;
    };

    auto fwd = [](float d) {
        const glm::vec2 c = clipForwardZ(d, 4096.0f);
        return c.x / c.y;
    };

    // Measured (float32, near 0.1, far 4096): forward-Z cannot separate surfaces closer than
    // ~0.38 units at 1 km — a THIRD OF A VOXEL — and ~3.76 units at 4 km, which is why distant
    // geometry z-fights. Reverse-Z resolves 3.3e-5 and 1.3e-4 there. Ratios ~11,000x and ~29,500x.
    // The thresholds below are deliberately far looser than the measurement so this pins the
    // order of magnitude rather than the exact float behaviour of one compiler.
    const float revSep1k = distinguishable(1000.0f, ndcDepthReverseZ);
    const float fwdSep1k = distinguishable(1000.0f, fwd);
    EXPECT_LT(revSep1k * 100.0f, fwdSep1k)
        << "reverse-Z resolved " << revSep1k << " units at 1km; forward-Z needed " << fwdSep1k;

    // Forward-Z's absolute failure is the point, not just the ratio: it cannot resolve a voxel.
    EXPECT_GT(fwdSep1k, 0.1f) << "forward-Z at 1km should be unable to resolve a fraction of a voxel";
    EXPECT_LT(revSep1k, 0.01f) << "reverse-Z at 1km should resolve far finer than a voxel";

    const float revSep4k = distinguishable(4000.0f, ndcDepthReverseZ);
    const float fwdSep4k = distinguishable(4000.0f, fwd);
    EXPECT_LT(revSep4k * 100.0f, fwdSep4k);
    EXPECT_GT(fwdSep4k, 1.0f) << "forward-Z at 4km should be unable to resolve a whole voxel";
}

TEST(ReverseZDepth, DepthStateMatchesTheProjection) {
    // The three pieces must agree or the screen goes black: clear to the FAR value, and test
    // GREATER so nearer (larger) depth wins. Pinned together because getting one of the three
    // wrong is the classic reverse-Z bug and each is edited in a different file.
    EXPECT_EQ(DC::kSceneDepthClear, DC::kReverseZ ? 0.0f : 1.0f);
    EXPECT_EQ(DC::sceneDepthCompareOp(),
              DC::kReverseZ ? VK_COMPARE_OP_GREATER : VK_COMPARE_OP_LESS);
    EXPECT_EQ(DC::sceneDepthCompareOpEqual(),
              DC::kReverseZ ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS_OR_EQUAL);

    // The clear value must be the depth of "infinitely far", or the sky would occlude the world.
    if (DC::kReverseZ) EXPECT_LT(ndcDepthReverseZ(1.0e7f), 1.0e-6f);
}

TEST(ReverseZDepth, CameraPerspectivePathIgnoresTheFarArgument) {
    // Callers still pass a far plane (it means something to the ortho rigs and to culling radii),
    // but the perspective projection is infinite, so it must not depend on the value. If someone
    // reintroduces a far term, this catches it.
    Camera cam;
    const glm::mat4 a = cam.getProjectionMatrix(kAspect, kNear, 256.0f);
    const glm::mat4 b = cam.getProjectionMatrix(kAspect, kNear, 4096.0f);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            ASSERT_FLOAT_EQ(a[c][r], b[c][r]) << "far plane leaked into the projection at [" << c << "][" << r << "]";
}
