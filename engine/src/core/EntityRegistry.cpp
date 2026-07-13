#include "core/EntityRegistry.h"
#include "scene/Entity.h"
#include "core/HealthComponent.h"
#include "core/Uuid.h"
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
    return registerEntity(entity, id, typeTag, "");
}

bool EntityRegistry::registerEntity(Scene::Entity* entity, const std::string& id, const std::string& typeTag,
                                    const std::string& uuid) {
    if (!entity) {
        LOG_WARN("EntityRegistry", "Attempted to register null entity with id: {}", id);
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_entities.find(id) != m_entities.end()) {
        LOG_WARN("EntityRegistry", "Entity ID already taken: {}", id);
        return false;
    }

    // Remove any previous registration for this entity pointer (drop its uuid index too)
    auto reverseIt = m_reverseMap.find(entity);
    if (reverseIt != m_reverseMap.end()) {
        auto oldIt = m_entities.find(reverseIt->second);
        if (oldIt != m_entities.end()) m_uuidToId.erase(oldIt->second.uuid);
        m_entities.erase(reverseIt->second);
        m_reverseMap.erase(reverseIt);
    }

    // Mint a stable uuid when none is supplied (create path); keep the caller's when
    // restoring a persisted/authored entity so its identity survives reload.
    std::string u = uuid;
    if (u.empty()) {
        u = Core::Uuid::generate();
        while (m_uuidToId.count(u)) u = Core::Uuid::generate();
    }

    m_entities[id] = EntityEntry{entity, typeTag, u};
    m_reverseMap[entity] = id;
    m_uuidToId[u] = id;

    LOG_DEBUG("EntityRegistry", "Registered entity '{}' (type: {})", id, (typeTag.empty() ? "none" : typeTag));
    return true;
}

bool EntityRegistry::unregisterEntity(const std::string& idOrUuid) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_entities.find(resolveIdLocked(idOrUuid));
    if (it == m_entities.end()) {
        return false;
    }

    m_reverseMap.erase(it->second.entity);
    m_uuidToId.erase(it->second.uuid);
    m_entities.erase(it);

    LOG_DEBUG("EntityRegistry", "Unregistered entity: {}", idOrUuid);
    return true;
}

bool EntityRegistry::unregisterEntity(Scene::Entity* entity) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_reverseMap.find(entity);
    if (it == m_reverseMap.end()) {
        return false;
    }

    auto entIt = m_entities.find(it->second);
    if (entIt != m_entities.end()) m_uuidToId.erase(entIt->second.uuid);
    m_entities.erase(it->second);
    m_reverseMap.erase(it);
    return true;
}

// ============================================================================
// Lookup
// ============================================================================

std::string EntityRegistry::resolveIdLocked(const std::string& idOrUuid) const {
    // m_mutex must already be held by caller.
    if (Core::Uuid::isValid(idOrUuid)) {
        auto it = m_uuidToId.find(idOrUuid);
        return (it != m_uuidToId.end()) ? it->second : std::string();  // unknown uuid → no match
    }
    return idOrUuid;  // legacy id ("player" / "entity_N" / "npc_<name>") — used verbatim
}

Scene::Entity* EntityRegistry::getEntity(const std::string& idOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entities.find(resolveIdLocked(idOrUuid));
    return (it != m_entities.end()) ? it->second.entity : nullptr;
}

std::string EntityRegistry::getUuid(const std::string& idOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_entities.find(resolveIdLocked(idOrUuid));
    return (it != m_entities.end()) ? it->second.uuid : std::string();
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
        obj["uuid"] = entry.uuid;
        obj["type"] = entry.typeTag;
        if (entry.entity) {
            auto pos = entry.entity->getPosition();
            obj["position"] = {{"x", pos.x}, {"y", pos.y}, {"z", pos.z}};
        }
        arr.push_back(obj);
    }
    return arr;
}

json EntityRegistry::entityToJson(const std::string& idOrUuid) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string id = resolveIdLocked(idOrUuid);
    auto it = m_entities.find(id);
    if (it == m_entities.end()) {
        return json{{"error", "Entity not found: " + idOrUuid}};
    }

    json obj;
    obj["id"] = id;
    obj["uuid"] = it->second.uuid;
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
        auto* health = it->second.entity->getHealthComponent();
        if (health) {
            obj["health"] = health->toJson();
        }
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
    m_uuidToId.clear();
    LOG_INFO("EntityRegistry", "Cleared all entity registrations");
}

} // namespace Core
} // namespace Phyxel
