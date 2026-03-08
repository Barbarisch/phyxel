#include "core/EntityRegistry.h"
#include "scene/Entity.h"
#include "utils/Logger.h"

namespace Phyxel {
namespace Core {

// ============================================================================
// Registration
// ============================================================================

bool EntityRegistry::registerEntity(Scene::Entity* entity, const std::string& id) {
    return registerEntity(entity, id, "");
}

std::string EntityRegistry::registerEntity(Scene::Entity* entity) {
    std::string id = "entity_" + std::to_string(m_nextAutoId.fetch_add(1));
    registerEntity(entity, id, "");
    return id;
}

bool EntityRegistry::registerEntity(Scene::Entity* entity, const std::string& id, const std::string& typeTag) {
    if (!entity) {
        LOG_WARN("EntityRegistry", "Attempted to register null entity with id: " << id);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_entities.find(id) != m_entities.end()) {
        LOG_WARN("EntityRegistry", "Entity ID already taken: " << id);
        return false;
    }

    // Remove any previous registration for this entity pointer
    auto reverseIt = m_reverseMap.find(entity);
    if (reverseIt != m_reverseMap.end()) {
        m_entities.erase(reverseIt->second);
        m_reverseMap.erase(reverseIt);
    }

    m_entities[id] = EntityEntry{entity, typeTag};
    m_reverseMap[entity] = id;

    LOG_DEBUG("EntityRegistry", "Registered entity '" << id << "' (type: " << (typeTag.empty() ? "none" : typeTag) << ")");
    return true;
}

bool EntityRegistry::unregisterEntity(const std::string& id) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_entities.find(id);
    if (it == m_entities.end()) {
        return false;
    }

    m_reverseMap.erase(it->second.entity);
    m_entities.erase(it);

    LOG_DEBUG("EntityRegistry", "Unregistered entity: " << id);
    return true;
}

bool EntityRegistry::unregisterEntity(Scene::Entity* entity) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_reverseMap.find(entity);
    if (it == m_reverseMap.end()) {
        return false;
    }

    m_entities.erase(it->second);
    m_reverseMap.erase(it);
    return true;
}

// ============================================================================
// Lookup
// ============================================================================

Scene::Entity* EntityRegistry::getEntity(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entities.find(id);
    return (it != m_entities.end()) ? it->second.entity : nullptr;
}

std::string EntityRegistry::getEntityId(Scene::Entity* entity) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_reverseMap.find(entity);
    return (it != m_reverseMap.end()) ? it->second : "";
}

bool EntityRegistry::hasEntity(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entities.find(id) != m_entities.end();
}

size_t EntityRegistry::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entities.size();
}

// ============================================================================
// Queries
// ============================================================================

std::vector<std::string> EntityRegistry::getAllIds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> ids;
    ids.reserve(m_entities.size());
    for (const auto& [id, entry] : m_entities) {
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::pair<std::string, Scene::Entity*>> EntityRegistry::getEntitiesByType(const std::string& typeTag) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::pair<std::string, Scene::Entity*>> results;
    for (const auto& [id, entry] : m_entities) {
        if (entry.typeTag == typeTag) {
            results.emplace_back(id, entry.entity);
        }
    }
    return results;
}

std::vector<std::pair<std::string, Scene::Entity*>> EntityRegistry::getEntitiesNear(
    const glm::vec3& position, float radius) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    float radiusSq = radius * radius;
    std::vector<std::pair<std::string, Scene::Entity*>> results;
    for (const auto& [id, entry] : m_entities) {
        if (entry.entity) {
            glm::vec3 diff = entry.entity->getPosition() - position;
            if (glm::dot(diff, diff) <= radiusSq) {
                results.emplace_back(id, entry.entity);
            }
        }
    }
    return results;
}

json EntityRegistry::toJson() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    json arr = json::array();
    for (const auto& [id, entry] : m_entities) {
        json obj;
        obj["id"] = id;
        obj["type"] = entry.typeTag;
        if (entry.entity) {
            auto pos = entry.entity->getPosition();
            obj["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
        }
        arr.push_back(obj);
    }
    return arr;
}

json EntityRegistry::entityToJson(const std::string& id) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entities.find(id);
    if (it == m_entities.end()) {
        return json{{"error", "Entity not found: " + id}};
    }

    json obj;
    obj["id"] = id;
    obj["type"] = it->second.typeTag;
    if (it->second.entity) {
        auto pos = it->second.entity->getPosition();
        obj["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
        auto scale = it->second.entity->getScale();
        obj["scale"] = {{"x", scale.x}, {"y", scale.y}, {"z", scale.z}};
        auto rot = it->second.entity->getRotation();
        obj["rotation"] = {{"w", rot.w}, {"x", rot.x}, {"y", rot.y}, {"z", rot.z}};
        auto color = it->second.entity->debugColor;
        obj["debugColor"] = {{"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a}};
    }
    return obj;
}

// ============================================================================
// Iteration
// ============================================================================

void EntityRegistry::forEach(const std::function<void(const std::string& id, Scene::Entity* entity)>& fn) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& [id, entry] : m_entities) {
        fn(id, entry.entity);
    }
}

// ============================================================================
// Bulk Operations
// ============================================================================

void EntityRegistry::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entities.clear();
    m_reverseMap.clear();
    LOG_INFO("EntityRegistry", "Cleared all entity registrations");
}

} // namespace Core
} // namespace Phyxel
