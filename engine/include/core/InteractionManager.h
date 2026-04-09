#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>

namespace Phyxel {

namespace Scene { class Entity; class NPCEntity; }
namespace Core { class EntityRegistry; class PlacedObjectManager; class InteractionProfileManager; struct InteractionPoint; }

namespace Core {

/// Detects when the player is near an interactable NPC or world object and handles E-key interaction.
class InteractionManager {
public:
    using InteractCallback  = std::function<void(Scene::NPCEntity* npc)>;
    /// Called when player interacts with a seat.
    /// Per-state offsets allow fine-tuning position during each sit phase.
    using SeatCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId,
                                            const glm::vec3& seatAnchorPos,
                                            float facingYaw,
                                            const glm::vec3& sitDownOffset,
                                            const glm::vec3& sittingIdleOffset,
                                            const glm::vec3& sitStandUpOffset,
                                            float sitBlendDuration,
                                            float seatHeightOffset)>;
    /// Called when the player presses E on a door handle interaction point.
    using DoorCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId)>;

    InteractionManager() = default;

    /// Set the entity registry for spatial NPC queries.
    void setEntityRegistry(EntityRegistry* registry) { m_registry = registry; }

    /// Set the placed-object manager for seat/object interaction queries.
    void setPlacedObjectManager(PlacedObjectManager* mgr) { m_placedObjects = mgr; }

    /// Set the interaction profile manager for per-archetype offset lookups.
    void setInteractionProfileManager(InteractionProfileManager* mgr) { m_profileManager = mgr; }

    /// Set the player's interaction archetype (e.g. "humanoid_normal").
    void setPlayerArchetype(const std::string& archetype) { m_playerArchetype = archetype; }
    const std::string& getPlayerArchetype() const { return m_playerArchetype; }

    /// Set a callback to fire when the player interacts with an NPC (e.g. open dialogue).
    void setInteractCallback(InteractCallback callback) { m_interactCallback = std::move(callback); }

    /// Set a callback to fire when the player sits down on a seat.
    void setSeatCallback(SeatCallback callback) { m_seatCallback = std::move(callback); }

    /// Set a callback to fire when the player interacts with a door handle.
    void setDoorCallback(DoorCallback callback) { m_doorCallback = std::move(callback); }

    /// Returns true if a door is in range and has priority (no NPC closer).
    bool isDoorInRange() const { return !m_nearestDoorObjId.empty(); }
    const std::string& getNearestDoorObjId() const { return m_nearestDoorObjId; }
    const std::string& getNearestDoorPtId() const { return m_nearestDoorPtId; }
    bool hasDoorCallback() const { return m_doorCallback != nullptr; }
    float getCooldownTimer() const { return m_cooldownTimer; }

    /// Call each frame with the player's current position, facing direction, and delta time.
    void update(float dt, const glm::vec3& playerPos, const glm::vec3& playerFront = glm::vec3(0,0,1));

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
    bool shouldShowPrompt() const { return m_nearestNPC != nullptr || isSeatInRange() || isDoorInRange(); }

    /// Get the prompt text for the currently active interaction (e.g. "Open/Close", "Sit").
    const std::string& getActivePromptText() const { return m_activePromptText; }

    /// Get the world position of the active interaction target (for screen-space prompt rendering).
    glm::vec3 getActivePromptWorldPos() const { return m_activePromptWorldPos; }

    /// Release the seat currently occupied by an entity (call when standing up).
    void releaseSeat(const std::string& occupantId);

    static constexpr float SEAT_INTERACT_RADIUS = 1.5f;   ///< Default seat interaction radius
    static constexpr float DOOR_INTERACT_RADIUS = 2.0f;   ///< Default door interaction radius
    static constexpr float DEFAULT_VIEW_ANGLE_HALF = 90.0f; ///< Default view cone half-angle (degrees)

private:
    EntityRegistry* m_registry = nullptr;
    PlacedObjectManager* m_placedObjects = nullptr;
    InteractionProfileManager* m_profileManager = nullptr;
    std::string m_playerArchetype = "humanoid_normal";
    Scene::NPCEntity* m_nearestNPC = nullptr;
    std::string m_nearestSeatObjId;
    std::string m_nearestSeatPtId;
    std::string m_nearestSeatTemplateName;  ///< template name of the object with the nearest seat
    glm::vec3 m_nearestSeatAnchorPos{0.0f};
    float m_nearestSeatFacingYaw = 0.0f;
    int m_nearestSeatObjectRotation = 0;
    glm::vec3 m_nearestSeatSitDownOffset{0.0f};
    glm::vec3 m_nearestSeatSittingIdleOffset{0.0f};
    glm::vec3 m_nearestSeatSitStandUpOffset{0.0f};
    float m_nearestSeatSitBlendDuration = 0.0f;
    float m_nearestSeatHeightOffset = 0.0f;
    std::string m_nearestDoorObjId;
    std::string m_nearestDoorPtId;

    std::string m_activePromptText;       ///< Resolved prompt text for current interaction
    glm::vec3 m_activePromptWorldPos{0.0f}; ///< World position of current interaction target

    InteractCallback m_interactCallback;
    SeatCallback     m_seatCallback;
    DoorCallback     m_doorCallback;
    float m_cooldownTimer = 0.0f;
    static constexpr float INTERACT_COOLDOWN = 0.5f;
};

} // namespace Core
} // namespace Phyxel
