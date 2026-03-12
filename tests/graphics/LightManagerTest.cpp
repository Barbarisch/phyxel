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

TEST(LightManagerTest, AddPointLight_CapacityLimit) {
    LightManager mgr;
    for (uint32_t i = 0; i < MAX_POINT_LIGHTS; i++) {
        EXPECT_GE(mgr.addPointLight(glm::vec3(static_cast<float>(i), 0, 0)), 0);
    }
    // Should fail at capacity
    EXPECT_EQ(mgr.addPointLight(glm::vec3(999, 0, 0)), -1);
    EXPECT_EQ(mgr.getPointLightCount(), MAX_POINT_LIGHTS);
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

TEST(LightManagerTest, AddSpotLight_CapacityLimit) {
    LightManager mgr;
    for (uint32_t i = 0; i < MAX_SPOT_LIGHTS; i++) {
        EXPECT_GE(mgr.addSpotLight(glm::vec3(static_cast<float>(i), 0, 0), glm::vec3(0, -1, 0)), 0);
    }
    EXPECT_EQ(mgr.addSpotLight(glm::vec3(999, 0, 0), glm::vec3(0, -1, 0)), -1);
    EXPECT_EQ(mgr.getSpotLightCount(), MAX_SPOT_LIGHTS);
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
