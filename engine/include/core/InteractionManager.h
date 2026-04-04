#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Scene { class Entity; class NPCEntity; }
namespace Core { class EntityRegistry; class PlacedObjectManager; struct InteractionPoint; }

namespace Core {

/// Detects when the player is near an interactable NPC or world object and handles E-key interaction.
class InteractionManager {
public:
    using InteractCallback  = std::function<void(Scene::NPCEntity* npc)>;
    /// Called when player interacts with a seat: (objectId, pointId, approachPos, facingYaw)
    /// approachPos is the world position the character should stand at before/during sit-down.
    using SeatCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId,
                                            const glm::vec3& approachPos,
                                            float facingYaw)>;

    InteractionManager() = default;

    /// Set the entity registry for spatial NPC queries.
    void setEntityRegistry(EntityRegistry* registry) { m_registry = registry; }

    /// Set the placed-object manager for seat/object interaction queries.
    void setPlacedObjectManager(PlacedObjectManager* mgr) { m_placedObjects = mgr; }

    /// Set a callback to fire when the player interacts with an NPC (e.g. open dialogue).
    void setInteractCallback(InteractCallback callback) { m_interactCallback = std::move(callback); }

    /// Set a callback to fire when the player sits down on a seat.
    void setSeatCallback(SeatCallback callback) { m_seatCallback = std::move(callback); }

    /// Call each frame with the player's current position and delta time.
    void update(float dt, const glm::vec3& playerPos);

    /// Called when the player presses the interact key (E).
    /// Priority: NPC > seat. Whichever is closer wins if both in range.
    void tryInteract(Scene::Entity* playerEntity);

    /// Trigger the interaction callback directly for a specific NPC (for API/testing).
    void triggerInteraction(Scene::NPCEntity* npc);

    /// Returns the NPC currently in interaction range, or nullptr.
    Scene::NPCEntity* getNearestInteractableNPC() const { return m_nearestNPC; }

    /// Returns true if a seat is in range and has priority over NPC (or NPC absent).
    bool isSeatInRange() const { return !m_nearestSeatObjId.empty(); }

    /// Whether a "Press E to interact" prompt should be shown.
    bool shouldShowPrompt() const { return m_nearestNPC != nullptr || isSeatInRange(); }

    /// Release the seat currently occupied by an entity (call when standing up).
    void releaseSeat(const std::string& occupantId);

    static constexpr float SEAT_INTERACT_RADIUS = 2.5f;  ///< How close player must be to a seat

private:
    EntityRegistry* m_registry = nullptr;
    PlacedObjectManager* m_placedObjects = nullptr;
    Scene::NPCEntity* m_nearestNPC = nullptr;
    std::string m_nearestSeatObjId;
    std::string m_nearestSeatPtId;
    glm::vec3 m_nearestSeatApproachPos{0.0f};
    float m_nearestSeatFacingYaw = 0.0f;
    InteractCallback m_interactCallback;
    SeatCallback m_seatCallback;
    float m_cooldownTimer = 0.0f;
    static constexpr float INTERACT_COOLDOWN = 0.5f;
};

} // namespace Core
} // namespace Phyxel
