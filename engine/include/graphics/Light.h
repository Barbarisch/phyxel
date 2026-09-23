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
    /// G-157: the half-extent of the EMITTER'S OWN BODY, in world units -- the bounding-sphere
    /// radius of the emissive voxels this light was merged from. The shadow march excludes at
    /// most this much solid around the light so an emissive voxel does not shadow itself, and
    /// NO MORE, so a sconce mounted flush on a wall can no longer exclude the wall as well.
    /// The default is half a cube's diagonal: the largest single emissive voxel there is.
    float extent = 0.866f;
    bool enabled = true;
};

struct SpotLight {
    int id = -1;  // Assigned by LightManager
    /// See PointLight::extent. Spot lights carry it in outerConeAndPadding.y, which was padding.
    float extent = 0.866f;
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
/// 48 bytes. MUST match the PointLightGPU declared in every shader that reads the light buffer:
/// voxel.frag, character.frag, foliage.frag, grass.frag, mirror_voxel.frag,
/// transparent_voxel.frag and gi_probe.comp. A mismatch reads garbage, it does not fail to link.
struct alignas(16) PointLightGPU {
    glm::vec4 positionAndRadius;     // xyz = position, w = radius
    glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
    glm::vec4 extentAndPad;          // x = emitter half-extent (G-157), yzw = padding
};

/// GPU-packed spot light for SSBO upload (std430 layout compatible)
struct alignas(16) SpotLightGPU {
    glm::vec4 positionAndRadius;     // xyz = position, w = radius
    glm::vec4 directionAndInnerCone; // xyz = direction, w = innerCone
    glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
    glm::vec4 outerConeAndPadding;   // x = outerCone, y = emitter half-extent (G-157), zw = pad
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
