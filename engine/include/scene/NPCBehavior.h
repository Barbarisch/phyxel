#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {

// Forward declarations
namespace Core { class EntityRegistry; }
namespace Graphics { class LightManager; }
namespace UI { class SpeechBubbleManager; }

namespace Scene {

class Entity;

/// Context passed to NPC behaviors each frame, providing access to engine systems.
struct NPCContext {
    Entity* self = nullptr;
    std::string selfId;
    Core::EntityRegistry* entityRegistry = nullptr;
    Graphics::LightManager* lightManager = nullptr;
    UI::SpeechBubbleManager* speechBubbleManager = nullptr;

    /// Lookup position of another entity by ID (may be null if registry unavailable).
    std::function<glm::vec3(const std::string&)> getEntityPosition;
};

/// Abstract interface for NPC behavior strategies.
/// Concrete implementations define how an NPC acts each frame.
class NPCBehavior {
public:
    virtual ~NPCBehavior() = default;

    /// Called each frame with delta time and the NPC's context.
    virtual void update(float dt, NPCContext& ctx) = 0;

    /// Called when a player or other entity interacts with this NPC.
    virtual void onInteract(Entity* interactor) = 0;

    /// Called when a game event is broadcast to this NPC.
    virtual void onEvent(const std::string& eventType, const nlohmann::json& data) = 0;

    /// Returns a human-readable name for this behavior type.
    virtual std::string getBehaviorName() const = 0;
};

} // namespace Scene
} // namespace Phyxel
