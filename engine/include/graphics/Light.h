#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Phyxel {
namespace Graphics {

/// Maximum number of point lights supported simultaneously
static constexpr uint32_t MAX_POINT_LIGHTS = 32;
/// Maximum number of spot lights supported simultaneously
static constexpr uint32_t MAX_SPOT_LIGHTS = 16;

struct PointLight {
    int id = -1;  // Assigned by LightManager
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float radius = 10.0f;
    bool enabled = true;
};

struct SpotLight {
    int id = -1;  // Assigned by LightManager
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    float radius = 20.0f;
    float innerCone = 0.9f;   // cos(~25 degrees)
    float outerCone = 0.8f;   // cos(~37 degrees)
    bool enabled = true;
};

/// GPU-packed point light for SSBO upload (std430 layout compatible)
struct alignas(16) PointLightGPU {
    glm::vec4 positionAndRadius;     // xyz = position, w = radius
    glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
};

/// GPU-packed spot light for SSBO upload (std430 layout compatible)
struct alignas(16) SpotLightGPU {
    glm::vec4 positionAndRadius;     // xyz = position, w = radius
    glm::vec4 directionAndInnerCone; // xyz = direction, w = innerCone
    glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
    glm::vec4 outerConeAndPadding;   // x = outerCone, yzw = padding
};

/// GPU light buffer layout matching the SSBO in shaders (std430)
struct LightBufferGPU {
    uint32_t numPointLights = 0;
    uint32_t numSpotLights = 0;
    uint32_t _pad0 = 0;
    uint32_t _pad1 = 0;
    PointLightGPU pointLights[MAX_POINT_LIGHTS];
    SpotLightGPU spotLights[MAX_SPOT_LIGHTS];
};

} // namespace Graphics
} // namespace Phyxel
