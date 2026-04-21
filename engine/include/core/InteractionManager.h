#pragma once

#include <string>
#include <functional>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include "core/InteractionHandler.h"

namespace Phyxel {

namespace Scene { class Entity; class NPCEntity; }
namespace Core { class EntityRegistry; class PlacedObjectManager; class InteractionProfileManager; struct InteractionPoint; }

namespace Core {

/// Detects when the player is near an interactable NPC or world object and handles E-key interaction.
///
/// Detection is driven by the InteractionHandlerRegistry: each registered handler type
/// is scanned for nearby interaction points. The highest-priority candidate wins.
class InteractionManager {
public:
    // Legacy callback typedefs — kept for backward compatibility.
    // New code should register handlers via InteractionHandlerRegistry instead.
    using InteractCallback  = std::function<void(Scene::NPCEntity* npc)>;
    using SeatCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId,
                                            const glm::vec3& seatAnchorPos,
                                            float facingYaw,
                                            const glm::vec3& sitDownOffset,
                                            const glm::vec3& sittingIdleOffset,
                                            const glm::vec3& sitStandUpOffset,
                                            float sitBlendDuration,
                                            float seatHeightOffset)>;
    using DoorCallback = std::function<void(const std::string& objectId,
                                            const std::string& pointId)>;

    InteractionManager() = default;

    /// Set the handler registry. Must be set before update/tryInteract are called.
    void setHandlerRegistry(InteractionHandlerRegistry* registry) { m_handlerRegistry = registry; }
    InteractionHandlerRegistry* getHandlerRegistry() const { return m_handlerRegistry; }

    /// Set the entity registry for spatial NPC queries.
    void setEntityRegistry(EntityRegistry* registry) { m_registry = registry; }

    /// Set the placed-object manager for interaction point queries.
    void setPlacedObjectManager(PlacedObjectManager* mgr) { m_placedObjects = mgr; }

    /// Set the interaction profile manager for per-archetype offset lookups.
    void setInteractionProfileManager(InteractionProfileManager* mgr) { m_profileManager = mgr; }

    /// Set the player's interaction archetype (e.g. "humanoid_normal").
    void setPlayerArchetype(const std::string& archetype) { m_playerArchetype = archetype; }
    const std::string& getPlayerArchetype() const { return m_playerArchetype; }

    /// Legacy callback setters — delegate to the appropriate handler in the registry.
    void setInteractCallback(InteractCallback callback);
    void setSeatCallback(SeatCallback callback);
    void setDoorCallback(DoorCallback callback);

    /// Returns true if a door is in range.
    bool isDoorInRange() const { return m_nearest.found && m_nearest.type == "door_handle"; }
    const std::string& getNearestDoorObjId() const { return isDoorInRange() ? m_nearest.objectId : s_empty; }
    const std::string& getNearestDoorPtId() const { return isDoorInRange() ? m_nearest.pointId : s_empty; }
    bool hasDoorCallback() const;
    float getCooldownTimer() const { return m_cooldownTimer; }

    /// Call each frame with the player's current position, facing direction, and delta time.
    void update(float dt, const glm::vec3& playerPos, const glm::vec3& playerFront = glm::vec3(0,0,1));

    /// Called when the player presses the interact key (E).
    void tryInteract(Scene::Entity* playerEntity);

    /// Trigger the interaction callback directly for a specific NPC (for API/testing).
    void triggerInteraction(Scene::NPCEntity* npc);

    /// Returns the NPC currently in interaction range, or nullptr.
    Scene::NPCEntity* getNearestInteractableNPC() const { return m_nearestNPC; }

    /// Returns true if a seat is in range.
    bool isSeatInRange() const { return m_nearest.found && m_nearest.type == "seat"; }

    /// Whether a "Press E to interact" prompt should be shown.
    bool shouldShowPrompt() const { return m_nearestNPC != nullptr || m_nearest.found; }

    /// Get the prompt text for the currently active interaction.
    const std::string& getActivePromptText() const { return m_activePromptText; }

    /// Get the world position of the active interaction target (for screen-space prompt rendering).
    glm::vec3 getActivePromptWorldPos() const { return m_activePromptWorldPos; }

    /// Release the seat currently occupied by an entity (call when standing up).
    void releaseSeat(const std::string& occupantId);

    static constexpr float DEFAULT_VIEW_ANGLE_HALF = 90.0f; ///< Default view cone half-angle (degrees)

private:
    /// Nearest detected placed-object interaction candidate.
    struct NearestCandidate {
        bool found = false;
        std::string type;           ///< Interaction type (e.g. "seat", "door_handle")
        std::string objectId;       ///< PlacedObject ID
        std::string pointId;        ///< InteractionPoint ID
        std::string templateName;   ///< Template name
        glm::vec3 worldPos{0.0f};   ///< World position of the point
        float facingYaw = 0.0f;
        int objectRotation = 0;
        std::string promptText;     ///< Custom prompt from point, or empty
        nlohmann::json typeData;    ///< Type-specific data (sit offsets, etc.)
        int priority = 0;           ///< Handler priority

        void clear() { *this = NearestCandidate{}; }
    };

    /// Build typeData JSON from an InteractionPoint's seat-specific fields.
    static nlohmann::json buildSeatTypeData(const InteractionPoint& pt);

    EntityRegistry* m_registry = nullptr;
    PlacedObjectManager* m_placedObjects = nullptr;
    InteractionProfileManager* m_profileManager = nullptr;
    InteractionHandlerRegistry* m_handlerRegistry = nullptr;
    std::string m_playerArchetype = "humanoid_normal";

    Scene::NPCEntity* m_nearestNPC = nullptr;
    NearestCandidate m_nearest;

    std::string m_activePromptText;
    glm::vec3 m_activePromptWorldPos{0.0f};

    // Fallback callbacks used when legacy set*Callback is called before registry is set.
    // Once the registry is configured, these are forwarded to the appropriate handlers.
    InteractCallback m_fallbackNPCCallback;
    SeatCallback     m_fallbackSeatCallback;
    DoorCallback     m_fallbackDoorCallback;

    float m_cooldownTimer = 0.0f;
    static constexpr float INTERACT_COOLDOWN = 0.5f;
    static inline const std::string s_empty;
};

} // namespace Core
} // namespace Phyxel
