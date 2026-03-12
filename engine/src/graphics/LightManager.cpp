#include "graphics/LightManager.h"
#include "utils/Logger.h"
#include <algorithm>

namespace Phyxel {
namespace Graphics {

// --- Point Lights ---

int LightManager::addPointLight(const PointLight& light) {
    if (pointLights_.size() >= MAX_POINT_LIGHTS) {
        LOG_WARN("LightManager", "Cannot add point light: at capacity ({})", MAX_POINT_LIGHTS);
        return -1;
    }
    int id = nextId_++;
    pointLights_.push_back({id, light});
    dirty_ = true;
    LOG_DEBUG("LightManager", "Added point light id={} at ({:.1f}, {:.1f}, {:.1f})",
              id, light.position.x, light.position.y, light.position.z);
    return id;
}

int LightManager::addPointLight(const glm::vec3& position, const glm::vec3& color,
                                float intensity, float radius) {
    PointLight light;
    light.position = position;
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    return addPointLight(light);
}

// --- Spot Lights ---

int LightManager::addSpotLight(const SpotLight& light) {
    if (spotLights_.size() >= MAX_SPOT_LIGHTS) {
        LOG_WARN("LightManager", "Cannot add spot light: at capacity ({})", MAX_SPOT_LIGHTS);
        return -1;
    }
    int id = nextId_++;
    spotLights_.push_back({id, light});
    dirty_ = true;
    LOG_DEBUG("LightManager", "Added spot light id={} at ({:.1f}, {:.1f}, {:.1f})",
              id, light.position.x, light.position.y, light.position.z);
    return id;
}

int LightManager::addSpotLight(const glm::vec3& position, const glm::vec3& direction,
                               const glm::vec3& color, float intensity, float radius,
                               float innerCone, float outerCone) {
    SpotLight light;
    light.position = position;
    light.direction = glm::normalize(direction);
    light.color = color;
    light.intensity = intensity;
    light.radius = radius;
    light.innerCone = innerCone;
    light.outerCone = outerCone;
    return addSpotLight(light);
}

// --- Common ---

bool LightManager::removeLight(int lightId) {
    // Try point lights first
    auto pit = std::find_if(pointLights_.begin(), pointLights_.end(),
                            [lightId](const PointLightEntry& e) { return e.id == lightId; });
    if (pit != pointLights_.end()) {
        pointLights_.erase(pit);
        dirty_ = true;
        LOG_DEBUG("LightManager", "Removed point light id={}", lightId);
        return true;
    }
    // Try spot lights
    auto sit = std::find_if(spotLights_.begin(), spotLights_.end(),
                            [lightId](const SpotLightEntry& e) { return e.id == lightId; });
    if (sit != spotLights_.end()) {
        spotLights_.erase(sit);
        dirty_ = true;
        LOG_DEBUG("LightManager", "Removed spot light id={}", lightId);
        return true;
    }
    return false;
}

bool LightManager::updatePointLight(int lightId, const PointLight& light) {
    auto* entry = findPointLight(lightId);
    if (!entry) return false;
    entry->light = light;
    dirty_ = true;
    return true;
}

bool LightManager::updateSpotLight(int lightId, const SpotLight& light) {
    auto* entry = findSpotLight(lightId);
    if (!entry) return false;
    entry->light = light;
    dirty_ = true;
    return true;
}

bool LightManager::updatePointLightPosition(int lightId, const glm::vec3& position) {
    auto* entry = findPointLight(lightId);
    if (!entry) return false;
    entry->light.position = position;
    dirty_ = true;
    return true;
}

bool LightManager::setLightEnabled(int lightId, bool enabled) {
    if (auto* pl = findPointLight(lightId)) {
        pl->light.enabled = enabled;
        dirty_ = true;
        return true;
    }
    if (auto* sl = findSpotLight(lightId)) {
        sl->light.enabled = enabled;
        dirty_ = true;
        return true;
    }
    return false;
}

const PointLight* LightManager::getPointLight(int lightId) const {
    const auto* entry = findPointLight(lightId);
    return entry ? &entry->light : nullptr;
}

const SpotLight* LightManager::getSpotLight(int lightId) const {
    const auto* entry = findSpotLight(lightId);
    return entry ? &entry->light : nullptr;
}

void LightManager::clear() {
    pointLights_.clear();
    spotLights_.clear();
    dirty_ = true;
}

std::vector<int> LightManager::getPointLightIds() const {
    std::vector<int> ids;
    ids.reserve(pointLights_.size());
    for (const auto& e : pointLights_) ids.push_back(e.id);
    return ids;
}

std::vector<int> LightManager::getSpotLightIds() const {
    std::vector<int> ids;
    ids.reserve(spotLights_.size());
    for (const auto& e : spotLights_) ids.push_back(e.id);
    return ids;
}

std::vector<PointLight> LightManager::getPointLights() const {
    std::vector<PointLight> result;
    result.reserve(pointLights_.size());
    for (const auto& e : pointLights_) {
        PointLight pl = e.light;
        pl.id = e.id;
        result.push_back(pl);
    }
    return result;
}

std::vector<SpotLight> LightManager::getSpotLights() const {
    std::vector<SpotLight> result;
    result.reserve(spotLights_.size());
    for (const auto& e : spotLights_) {
        SpotLight sl = e.light;
        sl.id = e.id;
        result.push_back(sl);
    }
    return result;
}

// --- GPU Upload ---

const LightBufferGPU& LightManager::getGPUData() {
    if (!dirty_) return gpuBuffer_;

    gpuBuffer_ = {};
    uint32_t pi = 0;
    for (const auto& e : pointLights_) {
        if (!e.light.enabled) continue;
        if (pi >= MAX_POINT_LIGHTS) break;
        auto& gpu = gpuBuffer_.pointLights[pi];
        gpu.positionAndRadius = glm::vec4(e.light.position, e.light.radius);
        gpu.colorAndIntensity = glm::vec4(e.light.color, e.light.intensity);
        pi++;
    }
    gpuBuffer_.numPointLights = pi;

    uint32_t si = 0;
    for (const auto& e : spotLights_) {
        if (!e.light.enabled) continue;
        if (si >= MAX_SPOT_LIGHTS) break;
        auto& gpu = gpuBuffer_.spotLights[si];
        gpu.positionAndRadius = glm::vec4(e.light.position, e.light.radius);
        gpu.directionAndInnerCone = glm::vec4(e.light.direction, e.light.innerCone);
        gpu.colorAndIntensity = glm::vec4(e.light.color, e.light.intensity);
        gpu.outerConeAndPadding = glm::vec4(e.light.outerCone, 0.0f, 0.0f, 0.0f);
        si++;
    }
    gpuBuffer_.numSpotLights = si;

    dirty_ = false;
    return gpuBuffer_;
}

// --- Helpers ---

LightManager::PointLightEntry* LightManager::findPointLight(int id) {
    auto it = std::find_if(pointLights_.begin(), pointLights_.end(),
                           [id](const PointLightEntry& e) { return e.id == id; });
    return it != pointLights_.end() ? &(*it) : nullptr;
}

const LightManager::PointLightEntry* LightManager::findPointLight(int id) const {
    auto it = std::find_if(pointLights_.begin(), pointLights_.end(),
                           [id](const PointLightEntry& e) { return e.id == id; });
    return it != pointLights_.end() ? &(*it) : nullptr;
}

LightManager::SpotLightEntry* LightManager::findSpotLight(int id) {
    auto it = std::find_if(spotLights_.begin(), spotLights_.end(),
                           [id](const SpotLightEntry& e) { return e.id == id; });
    return it != spotLights_.end() ? &(*it) : nullptr;
}

const LightManager::SpotLightEntry* LightManager::findSpotLight(int id) const {
    auto it = std::find_if(spotLights_.begin(), spotLights_.end(),
                           [id](const SpotLightEntry& e) { return e.id == id; });
    return it != spotLights_.end() ? &(*it) : nullptr;
}

} // namespace Graphics
} // namespace Phyxel
