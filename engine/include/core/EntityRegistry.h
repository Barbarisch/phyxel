#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

namespace Scene {
    class Entity;
}

namespace Core {

using json = nlohmann::json;

// ============================================================================
// EntityRegistry
//
// Central registry that gives every entity a unique string ID and supports
// O(1) lookup by ID, spatial queries, and type queries.
//
// Thread-safety: All methods are thread-safe (guarded by internal mutex).
// The game loop owns entity lifetime — the registry holds raw pointers.
//
// Usage:
//   registry.registerEntity(entity, "player_01");   // explicit ID
//   registry.registerEntity(entity);                  // auto-generated ID
//   auto* e = registry.getEntity("player_01");
//   auto nearby = registry.getEntitiesNear(pos, 10.0f);
//   registry.unregisterEntity("player_01");
// ============================================================================

class EntityRegistry {
public:
    EntityRegistry() = default;
    ~EntityRegistry() = default;

    // Non-copyable
    EntityRegistry(const EntityRegistry&) = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;

    // ========================================================================
    // Registration
    // ========================================================================

    /// Register an entity with an explicit string ID.
    /// Returns false if the ID is already taken.
    bool registerEntity(Scene::Entity* entity, const std::string& id);

    /// Register an entity with an auto-generated ID.
    /// Returns the generated ID.
    std::string registerEntity(Scene::Entity* entity);

    /// Register an entity with an ID and a type tag (e.g., "player", "npc", "enemy").
    bool registerEntity(Scene::Entity* entity, const std::string& id, const std::string& typeTag);

    /// Unregister an entity by ID.
    /// Returns false if the ID was not found.
    bool unregisterEntity(const std::string& id);

    /// Unregister an entity by pointer (slower — linear scan).
    bool unregisterEntity(Scene::Entity* entity);

    // ========================================================================
    // Lookup
    // ========================================================================

    /// Get an entity by ID. Returns nullptr if not found.
    Scene::Entity* getEntity(const std::string& id) const;

    /// Get the ID of an entity. Returns empty string if not registered.
    std::string getEntityId(Scene::Entity* entity) const;

    /// Check if an ID is registered.
    bool hasEntity(const std::string& id) const;

    /// Get total number of registered entities.
    size_t size() const;

    // ========================================================================
    // Queries
    // ========================================================================

    /// Get all registered entity IDs.
    std::vector<std::string> getAllIds() const;

    /// Get all entities with a given type tag.
    std::vector<std::pair<std::string, Scene::Entity*>> getEntitiesByType(const std::string& typeTag) const;

    /// Get entities within a radius of a position.
    std::vector<std::pair<std::string, Scene::Entity*>> getEntitiesNear(
        const glm::vec3& position, float radius) const;

    /// Get a JSON snapshot of all entities (ID, position, type).
    json toJson() const;

    /// Get a JSON snapshot of a single entity.
    json entityToJson(const std::string& id) const;

    // ========================================================================
    // Iteration
    // ========================================================================

    /// Call a function for each registered entity.
    void forEach(const std::function<void(const std::string& id, Scene::Entity* entity)>& fn) const;

    // ========================================================================
    // Bulk Operations
    // ========================================================================

    /// Clear all registrations.
    void clear();

private:
    struct EntityEntry {
        Scene::Entity* entity = nullptr;
        std::string typeTag;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, EntityEntry> m_entities;
    std::unordered_map<Scene::Entity*, std::string> m_reverseMap; // entity → id
    std::atomic<uint64_t> m_nextAutoId{1};
};

} // namespace Core
} // namespace Phyxel
