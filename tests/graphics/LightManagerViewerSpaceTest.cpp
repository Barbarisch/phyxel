// LightManagerViewerSpaceTest.cpp — point lights must be uploaded in the space they are consumed in.
//
// THE BUG THIS PINS. The renderer is camera-relative: every position reaching the GPU is
// (world - camera), and `ubo.cameraPosition` is deliberately zero. Light positions, however, were
// uploaded in ABSOLUTE world space and then subtracted from camera-relative fragment positions
// (voxel.frag `toLight = lightPos - inWorldPos`, and the same in character.frag). Every point and
// spot light was therefore displaced by the camera's own world position: correct only near the
// origin, and increasingly wrong the further a world extends. At continental coordinates a torch
// lights nothing anywhere near itself.
//
// It survived unnoticed because every test world is built at the origin, where the bug is exactly
// zero. That is what makes it worth a test rather than a look.

#include <gtest/gtest.h>

#include "graphics/LightManager.h"

using namespace Phyxel::Graphics;

namespace {

PointLight makePoint(const glm::vec3& pos) {
    PointLight l;
    l.position = pos;
    l.color = glm::vec3(1.0f);
    l.intensity = 1.0f;
    l.radius = 10.0f;
    l.enabled = true;
    return l;
}

}  // namespace

// The defining relationship: what reaches the GPU is the light's position MINUS the viewer origin.
TEST(LightManagerViewerSpace, PointLightsAreUploadedRelativeToTheViewer) {
    LightManager lm;
    const glm::vec3 lightWorld(1000.0f, 40.0f, -250.0f);
    lm.addPointLight(makePoint(lightWorld));

    const glm::vec3 viewer(990.0f, 35.0f, -240.0f);
    lm.setViewerWorld(viewer);

    const auto& gpu = lm.getGPUData();
    ASSERT_EQ(gpu.numPointLights, 1u);
    const glm::vec3 uploaded(gpu.pointLights[0].positionAndRadius);
    EXPECT_NEAR(uploaded.x, lightWorld.x - viewer.x, 1e-3f);
    EXPECT_NEAR(uploaded.y, lightWorld.y - viewer.y, 1e-3f);
    EXPECT_NEAR(uploaded.z, lightWorld.z - viewer.z, 1e-3f);
    EXPECT_NEAR(gpu.pointLights[0].positionAndRadius.w, 10.0f, 1e-4f) << "radius must be untouched";
}

// At the origin the relative and absolute answers coincide — which is precisely why the bug hid.
// Pinned so nobody "simplifies" the subtraction away after testing only at the origin.
TEST(LightManagerViewerSpace, AtTheOriginRelativeAndAbsoluteAgree) {
    LightManager lm;
    const glm::vec3 lightWorld(5.0f, 3.0f, -2.0f);
    lm.addPointLight(makePoint(lightWorld));
    lm.setViewerWorld(glm::vec3(0.0f));

    const glm::vec3 uploaded(lm.getGPUData().pointLights[0].positionAndRadius);
    EXPECT_NEAR(uploaded.x, lightWorld.x, 1e-4f);
    EXPECT_NEAR(uploaded.z, lightWorld.z, 1e-4f);
}

// A light far from the origin must still land right next to a viewer standing beside it. This is the
// case that was broken: at continental coordinates the light was thrown thousands of units away.
TEST(LightManagerViewerSpace, ALightBesideAViewerIsNearTheOriginWhereverTheyAre) {
    LightManager lm;
    const glm::vec3 viewer(60400.0f, 70.0f, -38000.0f);
    lm.addPointLight(makePoint(viewer + glm::vec3(2.0f, 1.0f, 0.5f)));
    lm.setViewerWorld(viewer);

    const glm::vec3 uploaded(lm.getGPUData().pointLights[0].positionAndRadius);
    EXPECT_LT(glm::length(uploaded), 5.0f)
        << "a light two units from the viewer arrived " << glm::length(uploaded)
        << " units away — positions are not being relativized";
}

// The packed buffer is cached on a dirty flag, and it is expressed relative to the viewer, so
// MOVING THE VIEWER must invalidate it exactly as moving a light does. Without this the lights lag
// the camera by however long the cache survives.
TEST(LightManagerViewerSpace, MovingTheViewerInvalidatesTheCache) {
    LightManager lm;
    lm.addPointLight(makePoint(glm::vec3(100.0f, 0.0f, 0.0f)));

    lm.setViewerWorld(glm::vec3(0.0f));
    const glm::vec3 first(lm.getGPUData().pointLights[0].positionAndRadius);
    EXPECT_NEAR(first.x, 100.0f, 1e-4f);

    lm.setViewerWorld(glm::vec3(90.0f, 0.0f, 0.0f));
    const glm::vec3 second(lm.getGPUData().pointLights[0].positionAndRadius);
    EXPECT_NEAR(second.x, 10.0f, 1e-4f)
        << "the cached buffer was reused against a stale viewer origin";
}

// Spot lights travel through the same buffer and must be relativized too — they were equally broken.
TEST(LightManagerViewerSpace, SpotLightsAreRelativizedAsWell) {
    LightManager lm;
    SpotLight s;
    s.position = glm::vec3(500.0f, 20.0f, 500.0f);
    s.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    s.color = glm::vec3(1.0f);
    s.intensity = 1.0f;
    s.radius = 20.0f;
    s.enabled = true;
    lm.addSpotLight(s);

    const glm::vec3 viewer(495.0f, 18.0f, 502.0f);
    lm.setViewerWorld(viewer);

    const auto& gpu = lm.getGPUData();
    ASSERT_EQ(gpu.numSpotLights, 1u);
    const glm::vec3 uploaded(gpu.spotLights[0].positionAndRadius);
    EXPECT_NEAR(uploaded.x, 5.0f, 1e-3f);
    EXPECT_NEAR(uploaded.z, -2.0f, 1e-3f);
    // The DIRECTION is a vector, not a position, and must not be shifted.
    EXPECT_NEAR(gpu.spotLights[0].directionAndInnerCone.y, -1.0f, 1e-4f);
}
