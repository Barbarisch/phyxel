#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace Phyxel {
namespace Graphics {

/// Maximum number of point lights supported simultaneously
static constexpr uint32_t MAX_POINT_LIGHTS = 32;
/// Maximum number of spot lights supported simultaneously
static constexpr uint32_t MAX_SPOT_LIGHTS = 16;

/// Who registered a light. Required when a light is added (LightManager::addPointLight /
/// addSpotLight take it as their FIRST parameter, with no default), so an untagged creation site
/// does not compile. CPU-only: it is never uploaded, and PointLightGPU/SpotLightGPU are unchanged.
/// Feeds GET /api/debug/light_stats by_source (docs/PerfProgram2026-09.md, I3).
enum class LightSource : uint8_t {
    EmissiveVoxel = 0,  ///< emissive/burning voxels, registered by the chunk emitter reconcile
    Fixture,            ///< structure-generation fixtures (StructureForge place_lights)
    ItemEffect,         ///< declarative item effects (torch flame, hearth fire, auras)
    Vfx,                ///< transient VFX lights (projectiles, beams, fields, bursts)
    Api,                ///< HTTP/MCP add_point_light / add_spot_light, and tests
    Editor,             ///< the editor's lighting panel
    Count
};

inline const char* lightSourceName(LightSource s) {
    switch (s) {
        case LightSource::EmissiveVoxel: return "emissive_voxel";
        case LightSource::Fixture: return "fixture";
        case LightSource::ItemEffect: return "item_effect";
        case LightSource::Vfx: return "vfx";
        case LightSource::Api: return "api";
        case LightSource::Editor: return "editor";
        default: return "invalid";
    }
}

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
