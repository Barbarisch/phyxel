#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Scene { class Entity; class NPCEntity; }

namespace Core {

class PlacedObjectManager;
class InteractionProfileManager;
struct InteractionPoint;
struct PlacedObject;

/// Context passed to an interaction handler when detecting or executing an interaction.
struct InteractionContext {
    std::string objectId;            ///< PlacedObject ID (empty for NPC interactions)
    std::string pointId;             ///< InteractionPoint ID within the object
    std::string templateName;        ///< Template name of the placed object
    Scene::Entity* playerEntity = nullptr;
    glm::vec3 worldPos{0.0f};       ///< World-space position of the interaction point
    float facingYaw = 0.0f;         ///< Facing yaw at the interaction point (after rotation)
    int objectRotation = 0;          ///< Object placement rotation (0/90/180/270)
    std::string playerArchetype;     ///< Player's interaction archetype (e.g. "humanoid_normal")

    /// Type-specific data from the InteractionPoint (sit offsets, etc.)
    nlohmann::json typeData;

    /// The NPC entity, if this is an NPC interaction.
    Scene::NPCEntity* npc = nullptr;
};

/// Base class for interaction handlers. Implement one per interaction type.
///
/// Each handler defines how a specific interaction type is detected, what prompt to show,
/// and what happens when the player executes the interaction.
class InteractionHandler {
public:
    virtual ~InteractionHandler() = default;

    /// The interaction type string (e.g. "seat", "door_handle", "npc").
    virtual const char* getType() const = 0;

    /// Default interaction radius. Individual points can override via interactionRadius field.
    virtual float getDefaultRadius() const = 0;

    /// Default prompt text shown when in range (e.g. "Sit down", "Open / Close").
    virtual const char* getDefaultPrompt() const = 0;

    /// Required animation clip names for this interaction type.
    virtual std::vector<std::string> getRequiredAnimations() const { return {}; }

    /// Priority weight for conflict resolution. Higher = checked first. Default 0.
    virtual int getPriority() const { return 0; }

    /// Execute the interaction. Called when the player presses E and this handler wins priority.
    virtual void execute(const InteractionContext& ctx) = 0;
};

/// Registry of interaction handlers, keyed by type string.
///
/// Handlers are registered at startup. The InteractionManager queries the registry
/// during detection and execution to dispatch to the correct handler.
class InteractionHandlerRegistry {
public:
    /// Register a handler for a type. Overwrites any existing handler for that type.
    void registerHandler(const std::string& type, std::unique_ptr<InteractionHandler> handler) {
        m_handlers[type] = std::move(handler);
    }

    /// Get the handler for a type, or nullptr if none registered.
    InteractionHandler* getHandler(const std::string& type) const {
        auto it = m_handlers.find(type);
        return it != m_handlers.end() ? it->second.get() : nullptr;
    }

    /// Get all registered type strings.
    std::vector<std::string> getRegisteredTypes() const {
        std::vector<std::string> types;
        types.reserve(m_handlers.size());
        for (const auto& [type, _] : m_handlers)
            types.push_back(type);
        return types;
    }

    /// Get all handlers (for iteration during detection).
    const std::unordered_map<std::string, std::unique_ptr<InteractionHandler>>& getHandlers() const {
        return m_handlers;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<InteractionHandler>> m_handlers;
};

} // namespace Core
} // namespace Phyxel
