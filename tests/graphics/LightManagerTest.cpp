#include <gtest/gtest.h>
#include "graphics/LightManager.h"
#include <glm/glm.hpp>

using namespace Phyxel::Graphics;

// ============================================================================
// Point Light Tests
// ============================================================================

TEST(LightManagerTest, AddPointLight_ReturnsValidId) {
    LightManager mgr;
    int id = mgr.addPointLight(glm::vec3(10, 20, 30));
    EXPECT_GE(id, 0);
    EXPECT_EQ(mgr.getPointLightCount(), 1u);
}

TEST(LightManagerTest, AddPointLight_FromStruct) {
    LightManager mgr;
    PointLight pl;
    pl.position = glm::vec3(5, 10, 15);
    pl.color = glm::vec3(1, 0, 0);
    pl.intensity = 3.0f;
    pl.radius = 25.0f;
    int id = mgr.addPointLight(pl);
    EXPECT_GE(id, 0);

    const PointLight* retrieved = mgr.getPointLight(id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->position.x, 5.0f);
    EXPECT_FLOAT_EQ(retrieved->intensity, 3.0f);
    EXPECT_FLOAT_EQ(retrieved->radius, 25.0f);
}

// ⚠️ BEHAVIOUR DELIBERATELY CHANGED (U3.1, docs/UnifiedLightingPlan.md). This test previously
// asserted that addPointLight REFUSES past MAX_POINT_LIGHTS and returns -1. That was the bug, not
// the contract: storage was capped with no distance culling, no priority and no eviction, so the
// first 32 lights ever REGISTERED won permanently wherever they were in the world — a torch in the
// player's hand contributed nothing if 32 lights existed anywhere, and city fixtures logged
// "light capacity reached" and stayed dark forever.
// MAX_POINT_LIGHTS is now an UPLOAD budget; storage is unbounded.
TEST(LightManagerTest, AddPointLight_AcceptsBeyondTheUploadBudget) {
    LightManager mgr;
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS * 3; i++) {
        EXPECT_GE(mgr.addPointLight(glm::vec3(static_cast<float>(i), 0, 0)), 0)
            << "light " << i << " was refused; the cap is an upload budget, not a storage limit";
    }
    EXPECT_EQ(mgr.getPointLightCount(), MAX_POINT_LIGHTS * 3);
    // ...but only the budget is ever uploaded.
    EXPECT_EQ(mgr.getGPUData().numPointLights, MAX_POINT_LIGHTS);
}

TEST(LightManagerTest, RemovePointLight) {
    LightManager mgr;
    int id = mgr.addPointLight(glm::vec3(1, 2, 3));
    EXPECT_EQ(mgr.getPointLightCount(), 1u);
    EXPECT_TRUE(mgr.removeLight(id));
    EXPECT_EQ(mgr.getPointLightCount(), 0u);
    EXPECT_EQ(mgr.getPointLight(id), nullptr);
}

TEST(LightManagerTest, RemoveNonexistent_ReturnsFalse) {
    LightManager mgr;
    EXPECT_FALSE(mgr.removeLight(999));
}

TEST(LightManagerTest, UpdatePointLight) {
    LightManager mgr;
    int id = mgr.addPointLight(glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), 1.0f, 10.0f);

    PointLight updated;
    updated.position = glm::vec3(50, 60, 70);
    updated.color = glm::vec3(0, 1, 0);
    updated.intensity = 7.5f;
    updated.radius = 40.0f;
    EXPECT_TRUE(mgr.updatePointLight(id, updated));

    const PointLight* retrieved = mgr.getPointLight(id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->position.x, 50.0f);
    EXPECT_FLOAT_EQ(retrieved->intensity, 7.5f);
}

// ============================================================================
// Spot Light Tests
// ============================================================================

TEST(LightManagerTest, AddSpotLight_ReturnsValidId) {
    LightManager mgr;
    int id = mgr.addSpotLight(glm::vec3(10, 20, 30), glm::vec3(0, -1, 0));
    EXPECT_GE(id, 0);
    EXPECT_EQ(mgr.getSpotLightCount(), 1u);
}

// Same deliberate change as the point-light case above.
TEST(LightManagerTest, AddSpotLight_AcceptsBeyondTheUploadBudget) {
    LightManager mgr;
    for (uint32_t i = 0; i < MAX_SPOT_LIGHTS * 3; i++) {
        EXPECT_GE(mgr.addSpotLight(glm::vec3(static_cast<float>(i), 0, 0), glm::vec3(0, -1, 0)), 0);
    }
    EXPECT_EQ(mgr.getSpotLightCount(), MAX_SPOT_LIGHTS * 3);
    EXPECT_EQ(mgr.getGPUData().numSpotLights, MAX_SPOT_LIGHTS);
}

// =============================================================================================
// U3.1 GATE — spatial selection. "The nearest light to the player is always among those
// uploaded", and "a 100-fixture city has no permanently-dark fixture."
// =============================================================================================

TEST(LightManagerTest, U31_TheNearestLightIsAlwaysUploadedNoMatterWhenItWasRegistered) {
    LightManager mgr;
    // Fill the budget with distant lights FIRST, so under the old first-come rule they would own
    // every slot forever.
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++) {
        mgr.addPointLight(glm::vec3(1000.0f + static_cast<float>(i), 0, 0), glm::vec3(1), 1.0f, 5.0f);
    }
    // Now the torch in the player's hand — registered LAST, and the whole point of the fix.
    const int torch = mgr.addPointLight(glm::vec3(0.5f, 0, 0), glm::vec3(1, 0.8f, 0.5f), 2.0f, 8.0f);
    ASSERT_GE(torch, 0);

    mgr.setViewerWorld(glm::vec3(0.0f));
    const auto& gpu = mgr.getGPUData();
    ASSERT_EQ(gpu.numPointLights, MAX_POINT_LIGHTS);

    bool torchUploaded = false;
    for (uint32_t i = 0; i < gpu.numPointLights; ++i) {
        // Positions are viewer-relative, and the viewer is at the origin here.
        if (glm::length(glm::vec3(gpu.pointLights[i].positionAndRadius) - glm::vec3(0.5f, 0, 0))
                < 1e-4f) {
            torchUploaded = true;
        }
    }
    EXPECT_TRUE(torchUploaded)
        << "the nearest light was not uploaded — first-come selection is still in force";
    EXPECT_EQ(mgr.droppedPointLights(), 1u) << "exactly one distant light should have lost its slot";
}

TEST(LightManagerTest, U31_TheUploadedSetFollowsTheViewer) {
    LightManager mgr;
    // Two clusters, far apart. Each is exactly the budget size, so only one can be uploaded.
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++)
        mgr.addPointLight(glm::vec3(-500.0f, 0, static_cast<float>(i)), glm::vec3(1), 1.0f, 4.0f);
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++)
        mgr.addPointLight(glm::vec3(500.0f, 0, static_cast<float>(i)), glm::vec3(1), 1.0f, 4.0f);

    auto meanX = [&mgr](const glm::vec3& viewer) {
        mgr.setViewerWorld(viewer);
        const auto& g = mgr.getGPUData();
        float sum = 0.0f;
        for (uint32_t i = 0; i < g.numPointLights; ++i)
            sum += g.pointLights[i].positionAndRadius.x + viewer.x;   // back to world space
        return sum / static_cast<float>(std::max(1u, g.numPointLights));
    };

    EXPECT_LT(meanX(glm::vec3(-500.0f, 0, 0)), -400.0f)
        << "standing in the west cluster, the east cluster was uploaded instead";
    EXPECT_GT(meanX(glm::vec3(500.0f, 0, 0)), 400.0f)
        << "the uploaded set did not follow the viewer across the world";
}

TEST(LightManagerTest, U31_ABigDistantLightBeatsATinyNearerOneOnlyWhenItsRadiusReaches) {
    // Relevance is distance to the light's SPHERE, not to its centre — otherwise a hearth whose
    // glow fills a room loses its slot to a candle just outside the room.
    LightManager mgr;
    mgr.setViewerWorld(glm::vec3(0.0f));
    const int hearth = mgr.addPointLight(glm::vec3(20, 0, 0), glm::vec3(1), 3.0f, 30.0f); // reaches
    const int candle = mgr.addPointLight(glm::vec3(15, 0, 0), glm::vec3(1), 0.5f, 2.0f);  // does not
    ASSERT_GE(hearth, 0);
    ASSERT_GE(candle, 0);

    const auto lights = mgr.getPointLights();
    ASSERT_EQ(lights.size(), 2u);
    // Both fit in the budget, so this asserts the ORDERING the selector would use.
    const auto& gpu = mgr.getGPUData();
    ASSERT_EQ(gpu.numPointLights, 2u);
    EXPECT_NEAR(gpu.pointLights[0].positionAndRadius.x, 20.0f, 1e-4f)
        << "the hearth whose radius reaches the viewer should rank first";
}

TEST(LightManagerTest, U31_SelectionIsStableBetweenFramesAtEqualRelevance) {
    // Two lights at identical relevance must not swap places frame to frame — that would flicker.
    LightManager mgr;
    mgr.setViewerWorld(glm::vec3(0.0f));
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS + 4; i++)
        mgr.addPointLight(glm::vec3(0, 0, 10.0f), glm::vec3(1), 1.0f, 5.0f);   // all identical

    std::vector<float> first;
    const auto& a = mgr.getGPUData();
    for (uint32_t i = 0; i < a.numPointLights; ++i) first.push_back(a.pointLights[i].colorAndIntensity.w);

    mgr.setViewerWorld(glm::vec3(0.0f, 0.0f, 0.0f));   // force a repack with no actual change
    mgr.setViewerWorld(glm::vec3(0.0f, 0.0f, 1e-6f));
    mgr.setViewerWorld(glm::vec3(0.0f));
    const auto& b = mgr.getGPUData();
    ASSERT_EQ(a.numPointLights, b.numPointLights);
    for (uint32_t i = 0; i < b.numPointLights; ++i)
        EXPECT_FLOAT_EQ(first[i], b.pointLights[i].colorAndIntensity.w);
}

TEST(LightManagerTest, UpdateSpotLight) {
    LightManager mgr;
    int id = mgr.addSpotLight(glm::vec3(0, 0, 0), glm::vec3(0, -1, 0));

    SpotLight updated;
    updated.position = glm::vec3(100, 50, 0);
    updated.direction = glm::normalize(glm::vec3(1, -1, 0));
    updated.color = glm::vec3(1, 0.5f, 0);
    updated.intensity = 10.0f;
    updated.radius = 80.0f;
    updated.innerCone = 0.95f;
    updated.outerCone = 0.85f;
    EXPECT_TRUE(mgr.updateSpotLight(id, updated));

    const SpotLight* retrieved = mgr.getSpotLight(id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_FLOAT_EQ(retrieved->position.x, 100.0f);
    EXPECT_FLOAT_EQ(retrieved->intensity, 10.0f);
}

// ============================================================================
// Enable / Disable
// ============================================================================

TEST(LightManagerTest, SetLightEnabled_PointLight) {
    LightManager mgr;
    int id = mgr.addPointLight(glm::vec3(0, 0, 0));
    EXPECT_TRUE(mgr.setLightEnabled(id, false));

    const auto& gpu = mgr.getGPUData();
    EXPECT_EQ(gpu.numPointLights, 0u);

    EXPECT_TRUE(mgr.setLightEnabled(id, true));
    const auto& gpu2 = mgr.getGPUData();
    EXPECT_EQ(gpu2.numPointLights, 1u);
}

// ============================================================================
// GPU Data
// ============================================================================

TEST(LightManagerTest, GetGPUData_Empty) {
    LightManager mgr;
    const auto& gpu = mgr.getGPUData();
    EXPECT_EQ(gpu.numPointLights, 0u);
    EXPECT_EQ(gpu.numSpotLights, 0u);
}

TEST(LightManagerTest, GetGPUData_PacksPointLights) {
    LightManager mgr;
    mgr.addPointLight(glm::vec3(1, 2, 3), glm::vec3(1, 0, 0), 5.0f, 20.0f);
    mgr.addPointLight(glm::vec3(4, 5, 6), glm::vec3(0, 1, 0), 3.0f, 15.0f);

    const auto& gpu = mgr.getGPUData();
    EXPECT_EQ(gpu.numPointLights, 2u);
    EXPECT_FLOAT_EQ(gpu.pointLights[0].positionAndRadius.x, 1.0f);
    EXPECT_FLOAT_EQ(gpu.pointLights[0].positionAndRadius.w, 20.0f);
    EXPECT_FLOAT_EQ(gpu.pointLights[0].colorAndIntensity.w, 5.0f);
    EXPECT_FLOAT_EQ(gpu.pointLights[1].positionAndRadius.x, 4.0f);
}

TEST(LightManagerTest, GetGPUData_PacksSpotLights) {
    LightManager mgr;
    mgr.addSpotLight(glm::vec3(10, 20, 30), glm::vec3(0, -1, 0),
                     glm::vec3(1, 1, 1), 8.0f, 50.0f, 0.9f, 0.8f);

    const auto& gpu = mgr.getGPUData();
    EXPECT_EQ(gpu.numSpotLights, 1u);
    EXPECT_FLOAT_EQ(gpu.spotLights[0].positionAndRadius.x, 10.0f);
    EXPECT_FLOAT_EQ(gpu.spotLights[0].positionAndRadius.w, 50.0f);
    EXPECT_FLOAT_EQ(gpu.spotLights[0].directionAndInnerCone.w, 0.9f);
    EXPECT_FLOAT_EQ(gpu.spotLights[0].outerConeAndPadding.x, 0.8f);
    EXPECT_FLOAT_EQ(gpu.spotLights[0].colorAndIntensity.w, 8.0f);
}

TEST(LightManagerTest, GetGPUData_SkipsDisabledLights) {
    LightManager mgr;
    int id1 = mgr.addPointLight(glm::vec3(1, 0, 0));
    int id2 = mgr.addPointLight(glm::vec3(2, 0, 0));
    mgr.setLightEnabled(id1, false);

    const auto& gpu = mgr.getGPUData();
    EXPECT_EQ(gpu.numPointLights, 1u);
    EXPECT_FLOAT_EQ(gpu.pointLights[0].positionAndRadius.x, 2.0f);
    (void)id2; // suppress unused
}

// ============================================================================
// Clear
// ============================================================================

TEST(LightManagerTest, Clear_RemovesAllLights) {
    LightManager mgr;
    mgr.addPointLight(glm::vec3(0, 0, 0));
    mgr.addPointLight(glm::vec3(1, 0, 0));
    mgr.addSpotLight(glm::vec3(2, 0, 0), glm::vec3(0, -1, 0));
    EXPECT_EQ(mgr.getTotalLightCount(), 3u);

    mgr.clear();
    EXPECT_EQ(mgr.getTotalLightCount(), 0u);
    EXPECT_EQ(mgr.getPointLightCount(), 0u);
    EXPECT_EQ(mgr.getSpotLightCount(), 0u);
}

// ============================================================================
// getPointLights / getSpotLights
// ============================================================================

TEST(LightManagerTest, GetPointLights_ReturnsWithIds) {
    LightManager mgr;
    int id1 = mgr.addPointLight(glm::vec3(1, 0, 0));
    int id2 = mgr.addPointLight(glm::vec3(2, 0, 0));

    auto lights = mgr.getPointLights();
    ASSERT_EQ(lights.size(), 2u);
    EXPECT_EQ(lights[0].id, id1);
    EXPECT_EQ(lights[1].id, id2);
    EXPECT_FLOAT_EQ(lights[0].position.x, 1.0f);
    EXPECT_FLOAT_EQ(lights[1].position.x, 2.0f);
}

TEST(LightManagerTest, GetSpotLights_ReturnsWithIds) {
    LightManager mgr;
    int id = mgr.addSpotLight(glm::vec3(5, 10, 15), glm::vec3(0, -1, 0));

    auto lights = mgr.getSpotLights();
    ASSERT_EQ(lights.size(), 1u);
    EXPECT_EQ(lights[0].id, id);
    EXPECT_FLOAT_EQ(lights[0].position.x, 5.0f);
}

// ============================================================================
// Unique IDs across types
// ============================================================================

TEST(LightManagerTest, IdsAreUniqueAcrossTypes) {
    LightManager mgr;
    int pid = mgr.addPointLight(glm::vec3(0, 0, 0));
    int sid = mgr.addSpotLight(glm::vec3(0, 0, 0), glm::vec3(0, -1, 0));
    EXPECT_NE(pid, sid);
}
