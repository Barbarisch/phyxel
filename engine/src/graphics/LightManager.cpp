#include "graphics/LightManager.h"
#include "utils/Logger.h"
#include <algorithm>

namespace Phyxel {
namespace Graphics {

// --- Point Lights ---

int LightManager::addPointLight(const PointLight& light) {
    // U3.1 — MAX_POINT_LIGHTS is an UPLOAD budget, not a storage limit.
    //
    // This used to refuse outright at 32 and return -1, with no distance culling, no priority and
    // no eviction: the first 32 lights ever REGISTERED won permanently, wherever they were in the
    // world. A torch in the player's hand contributed nothing if 32 lights had been created
    // anywhere first, and StructureForge logged "place_lights: light capacity reached" while whole
    // districts of a city stayed dark. Storage is now unbounded and getGPUData() selects the
    // MAX_POINT_LIGHTS most relevant lights for the viewer each frame.
    if (pointLights_.size() >= kStorageWarnThreshold && !warnedPointStorage_) {
        warnedPointStorage_ = true;
        LOG_WARN("LightManager", "{} point lights registered — this is a leak-detection warning, "
                                 "not a cap; uploads are still limited to {}",
                 pointLights_.size(), MAX_POINT_LIGHTS);
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
    // U3.1: same as point lights — the cap is an upload budget, not a storage limit.
    if (spotLights_.size() >= kStorageWarnThreshold && !warnedSpotStorage_) {
        warnedSpotStorage_ = true;
        LOG_WARN("LightManager", "{} spot lights registered — leak-detection warning, not a cap; "
                                 "uploads are still limited to {}",
                 spotLights_.size(), MAX_SPOT_LIGHTS);
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

void LightManager::setViewerWorld(const glm::vec3& viewerWorld) {
    // The packed buffer is cached on dirty_, and it is expressed relative to this origin -- so a
    // moved camera invalidates it just as surely as a moved light does. Missing this would cache
    // positions against a stale origin and the lights would lag the camera.
    if (viewerWorld != viewerWorld_) {
        viewerWorld_ = viewerWorld;
        dirty_ = true;
    }
}

float LightManager::relevance(const glm::vec3& position, float radius) const {
    // Distance from the viewer to the light's SPHERE OF INFLUENCE, not to the light itself.
    // Subtracting the radius is what stops a small torch two metres away from losing its slot to a
    // large hearth ten metres away: what matters is whether the light can reach the viewer's
    // surroundings at all. Negative means the viewer is inside the light's radius.
    // Lower is more relevant.
    return glm::length(position - viewerWorld_) - radius;
}

const LightBufferGPU& LightManager::getGPUData() {
    if (!dirty_) return gpuBuffer_;

    gpuBuffer_ = {};

    // U3.1 — SELECT the most relevant lights rather than taking the first N registered.
    //
    // Selection is by relevance to the VIEWER. That is an approximation: the correct question is
    // "does this light affect anything visible", which would need the light's sphere tested against
    // the view frustum. Viewer-relative is the standard cheap stand-in and it fixes the case that
    // actually bites — a light beside the player never being uploaded.
    //
    // partial_sort, not a full sort: only the first N matter and light counts can now be large.
    auto pickPoints = [this]() {
        std::vector<const PointLightEntry*> live;
        live.reserve(pointLights_.size());
        for (const auto& e : pointLights_) if (e.light.enabled) live.push_back(&e);
        const size_t take = std::min<size_t>(live.size(), MAX_POINT_LIGHTS);
        std::partial_sort(live.begin(), live.begin() + take, live.end(),
                          [this](const PointLightEntry* a, const PointLightEntry* b) {
                              const float ra = relevance(a->light.position, a->light.radius);
                              const float rb = relevance(b->light.position, b->light.radius);
                              // Tie-break on id so the selection is STABLE frame to frame: two
                              // lights at equal relevance must not swap places and flicker.
                              return (ra != rb) ? (ra < rb) : (a->id < b->id);
                          });
        live.resize(take);
        return live;
    };

    uint32_t pi = 0;
    for (const PointLightEntry* e : pickPoints()) {
        auto& gpu = gpuBuffer_.pointLights[pi];
        gpu.positionAndRadius = glm::vec4(e->light.position - viewerWorld_, e->light.radius);
        gpu.colorAndIntensity = glm::vec4(e->light.color, e->light.intensity);
        pi++;
    }
    gpuBuffer_.numPointLights = pi;

    std::vector<const SpotLightEntry*> liveSpots;
    liveSpots.reserve(spotLights_.size());
    for (const auto& e : spotLights_) if (e.light.enabled) liveSpots.push_back(&e);
    const size_t takeSpots = std::min<size_t>(liveSpots.size(), MAX_SPOT_LIGHTS);
    std::partial_sort(liveSpots.begin(), liveSpots.begin() + takeSpots, liveSpots.end(),
                      [this](const SpotLightEntry* a, const SpotLightEntry* b) {
                          const float ra = relevance(a->light.position, a->light.radius);
                          const float rb = relevance(b->light.position, b->light.radius);
                          return (ra != rb) ? (ra < rb) : (a->id < b->id);
                      });
    liveSpots.resize(takeSpots);

    uint32_t si = 0;
    for (const SpotLightEntry* e : liveSpots) {
        auto& gpu = gpuBuffer_.spotLights[si];
        gpu.positionAndRadius = glm::vec4(e->light.position - viewerWorld_, e->light.radius);
        gpu.directionAndInnerCone = glm::vec4(e->light.direction, e->light.innerCone);
        gpu.colorAndIntensity = glm::vec4(e->light.color, e->light.intensity);
        gpu.outerConeAndPadding = glm::vec4(e->light.outerCone, 0.0f, 0.0f, 0.0f);
        si++;
    }
    gpuBuffer_.numSpotLights = si;

    dirty_ = false;
    return gpuBuffer_;
}

size_t LightManager::droppedPointLights() const {
    size_t live = 0;
    for (const auto& e : pointLights_) if (e.light.enabled) ++live;
    return live > MAX_POINT_LIGHTS ? live - MAX_POINT_LIGHTS : 0;
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
