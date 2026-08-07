// Kinematic visibility predicate — L2.
//
// Guards the culling contract for kinematic objects (items/furniture/doors):
// inside-frustum near objects draw; behind-camera objects don't; beyond the
// distance cap doesn't; the cap can be disabled for shadow passes (an object
// behind the camera may still cast a visible shadow).

#include <gtest/gtest.h>

#include "graphics/KinematicCulling.h"

#include <glm/gtc/matrix_transform.hpp>

using namespace Phyxel;

namespace {

// A standard perspective frustum looking down -Z from the origin.
Utils::Frustum makeFrustum() {
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f,
                                            0.1f, 500.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1),
                                       glm::vec3(0, 1, 0));
    Utils::Frustum f;
    f.extractFromMatrix(proj * view, Utils::Frustum::ClipConvention::ForwardNegOneToOne);
    return f;
}

constexpr float kMaxDistSq = 128.0f * 128.0f;

}  // namespace

TEST(KinematicCulling, AheadInsideDistanceIsVisible) {
    auto f = makeFrustum();
    const glm::vec3 c(0, 0, -10);
    EXPECT_TRUE(Graphics::kinematicObjectVisible(f, c, 1.0f,
                                                 glm::dot(c, c), kMaxDistSq));
}

TEST(KinematicCulling, BehindCameraIsCulled) {
    auto f = makeFrustum();
    const glm::vec3 c(0, 0, +10);   // behind the -Z view
    EXPECT_FALSE(Graphics::kinematicObjectVisible(f, c, 1.0f,
                                                  glm::dot(c, c), kMaxDistSq));
}

TEST(KinematicCulling, BeyondDistanceCapIsCulled) {
    auto f = makeFrustum();
    const glm::vec3 c(0, 0, -200);  // ahead but past 128 u
    EXPECT_FALSE(Graphics::kinematicObjectVisible(f, c, 1.0f,
                                                  glm::dot(c, c), kMaxDistSq));
}

TEST(KinematicCulling, DistanceCapDisabledForShadowUse) {
    auto f = makeFrustum();
    const glm::vec3 c(0, 0, -200);
    EXPECT_TRUE(Graphics::kinematicObjectVisible(f, c, 1.0f,
                                                 glm::dot(c, c), /*maxDistSq=*/0.0f));
}

TEST(KinematicCulling, RadiusSavesEdgeStraddlers) {
    auto f = makeFrustum();
    // Just outside the left plane at z=-10, but a fat radius overlaps it.
    const glm::vec3 c(-12.0f, 0, -10);
    const float d2 = glm::dot(c, c);
    EXPECT_FALSE(Graphics::kinematicObjectVisible(f, c, 0.1f, d2, kMaxDistSq));
    EXPECT_TRUE(Graphics::kinematicObjectVisible(f, c, 8.0f, d2, kMaxDistSq));
}
