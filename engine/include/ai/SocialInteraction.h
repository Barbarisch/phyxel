#pragma once

#include "ai/NeedsSystem.h"
#include "ai/RelationshipManager.h"
#include "ai/WorldView.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <glm/vec3.hpp>

namespace Phyxel {

namespace Scene { class EntityRegistry; }

namespace AI {

/// Result of a social interaction between two NPCs.
struct SocialResult {
    std::string initiatorId;
    std::string targetId;
    InteractionType type = InteractionType::Greeting;
    float intensity = 1.0f;
    std::string description;        ///< What happened (for observations/logs)
    bool gossipShared = false;      ///< Whether knowledge was exchanged
};

/// Info about an NPC needed for social decisions (passed in or looked up).
struct SocialParticipant {
    std::string id;
    glm::vec3 position{0.0f};
    std::string currentActivity;    ///< From blackboard "currentActivity"
    NeedsSystem* needs = nullptr;
    WorldView* worldView = nullptr;
};

/// Centralized social interaction manager.
/// Each tick, checks for NPC pairs that are close enough and in a social activity,
/// then runs interactions that modify relationships, fulfill needs, and share gossip.
class SocialInteractionSystem {
public:
    /// Set proximity radius for social encounters (default 5.0 units).
    void setInteractionRadius(float radius) { m_interactionRadius = radius; }
    float getInteractionRadius() const { return m_interactionRadius; }

    /// Set minimum cooldown between interactions for the same pair (default 2.0 game hours).
    void setCooldownHours(float hours) { m_cooldownHours = hours; }

    /// Process one tick of social interactions.
    /// Finds eligible pairs among participants, determines interaction type,
    /// applies effects to relationships/needs/worldviews.
    /// Returns list of interactions that occurred.
    std::vector<SocialResult> update(
        float deltaHours,
        const std::vector<SocialParticipant>& participants,
        RelationshipManager& relationships);

    /// Determine what interaction type two NPCs would have, given their disposition.
    static InteractionType chooseInteraction(float disposition, float socialNeedUrgency);

    /// Share a random belief/observation from sharer's WorldView with listener.
    static void shareGossip(const WorldView& sharer, WorldView& listener, float gameTime);

    /// Clear cooldowns (e.g. on new day).
    void clearCooldowns() { m_cooldowns.clear(); }

private:
    float m_interactionRadius = 5.0f;
    float m_cooldownHours = 2.0f;

    /// Cooldown tracker: pair key → remaining hours
    std::unordered_map<std::string, float> m_cooldowns;

    static std::string makePairKey(const std::string& a, const std::string& b);
    bool isOnCooldown(const std::string& a, const std::string& b) const;
    void setCooldown(const std::string& a, const std::string& b);
    void decayCooldowns(float deltaHours);
};

} // namespace AI
} // namespace Phyxel
