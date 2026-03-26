#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Scene { class Entity; class NPCEntity; }
namespace Core { class EntityRegistry; }

namespace Core {

/// Detects when the player is near an interactable NPC and handles E-key interaction.
class InteractionManager {
public:
    using InteractCallback = std::function<void(Scene::NPCEntity* npc)>;

    InteractionManager() = default;

    /// Set the entity registry for spatial queries.
    void setEntityRegistry(EntityRegistry* registry) { m_registry = registry; }

    /// Set a callback to fire when the player interacts with an NPC (e.g. open dialogue).
    void setInteractCallback(InteractCallback callback) { m_interactCallback = std::move(callback); }

    /// Call each frame with the player's current position and delta time.
    /// Finds the nearest NPC tagged "npc" within interaction radius.
    void update(float dt, const glm::vec3& playerPos);

    /// Called when the player presses the interact key (E).
    /// Triggers onInteract on the nearest NPC if one is in range.
    void tryInteract(Scene::Entity* playerEntity);

    /// Trigger the interaction callback directly for a specific NPC (for API/testing).
    void triggerInteraction(Scene::NPCEntity* npc);

    /// Returns the NPC currently in interaction range, or nullptr.
    Scene::NPCEntity* getNearestInteractableNPC() const { return m_nearestNPC; }

    /// Whether a "Press E to interact" prompt should be shown.
    bool shouldShowPrompt() const { return m_nearestNPC != nullptr; }

private:
    EntityRegistry* m_registry = nullptr;
    Scene::NPCEntity* m_nearestNPC = nullptr;
    InteractCallback m_interactCallback;
    float m_cooldownTimer = 0.0f;
    static constexpr float INTERACT_COOLDOWN = 0.5f;
};

} // namespace Core
} // namespace Phyxel
