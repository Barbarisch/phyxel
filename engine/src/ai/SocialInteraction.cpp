#include "ai/SocialInteraction.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace AI {

// ============================================================================
// Pair key / cooldown helpers
// ============================================================================

std::string SocialInteractionSystem::makePairKey(const std::string& a, const std::string& b) {
    return (a < b) ? a + "<>" + b : b + "<>" + a;
}

bool SocialInteractionSystem::isOnCooldown(const std::string& a, const std::string& b) const {
    auto it = m_cooldowns.find(makePairKey(a, b));
    return it != m_cooldowns.end() && it->second > 0.0f;
}

void SocialInteractionSystem::setCooldown(const std::string& a, const std::string& b) {
    m_cooldowns[makePairKey(a, b)] = m_cooldownHours;
}

void SocialInteractionSystem::decayCooldowns(float deltaHours) {
    for (auto it = m_cooldowns.begin(); it != m_cooldowns.end(); ) {
        it->second -= deltaHours;
        if (it->second <= 0.0f) {
            it = m_cooldowns.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Main update tick
// ============================================================================

std::vector<SocialResult> SocialInteractionSystem::update(
    float deltaHours,
    const std::vector<SocialParticipant>& participants,
    RelationshipManager& relationships)
{
    decayCooldowns(deltaHours);

    std::vector<SocialResult> results;
    float radiusSq = m_interactionRadius * m_interactionRadius;

    // Check all pairs for proximity + social eligibility
    for (size_t i = 0; i < participants.size(); ++i) {
        for (size_t j = i + 1; j < participants.size(); ++j) {
            const auto& a = participants[i];
            const auto& b = participants[j];

            // At least one must be in a social-ish activity
            bool aSocial = (a.currentActivity == "Socialize" || a.currentActivity == "Eat" ||
                           a.currentActivity == "Shop");
            bool bSocial = (b.currentActivity == "Socialize" || b.currentActivity == "Eat" ||
                           b.currentActivity == "Shop");
            if (!aSocial && !bSocial) continue;

            // Distance check
            glm::vec3 diff = a.position - b.position;
            float distSq = diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
            if (distSq > radiusSq) continue;

            // Cooldown check
            if (isOnCooldown(a.id, b.id)) continue;

            // Determine disposition
            float disposition = relationships.getDisposition(a.id, b.id);

            // Get social need urgency of initiator
            float socialUrgency = 0.0f;
            if (a.needs) {
                const Need* sn = a.needs->getNeed(NeedType::Social);
                if (sn) socialUrgency = sn->getUrgency();
            }

            InteractionType type = chooseInteraction(disposition, socialUrgency);

            // Apply interaction (bidirectional: both NPCs are affected)
            float intensity = 1.0f;
            relationships.applyInteraction(a.id, b.id, type, intensity);
            relationships.applyInteraction(b.id, a.id, type, intensity);

            // Fulfill social needs for both
            if (a.needs) a.needs->fulfill(NeedType::Social, 15.0f);
            if (b.needs) b.needs->fulfill(NeedType::Social, 15.0f);

            // Build result
            SocialResult result;
            result.initiatorId = a.id;
            result.targetId = b.id;
            result.type = type;
            result.intensity = intensity;

            // Gossip exchange (positive interactions share knowledge)
            if (a.worldView && b.worldView && (type == InteractionType::Conversation ||
                type == InteractionType::Gossip)) {
                shareGossip(*a.worldView, *b.worldView, 0.0f);
                shareGossip(*b.worldView, *a.worldView, 0.0f);
                result.gossipShared = true;
            }

            result.description = a.id + " and " + b.id + " " +
                interactionTypeToString(type);

            // Record observations in both world views
            if (a.worldView) {
                Observation obs;
                obs.eventId = "social_" + a.id + "_" + b.id;
                obs.description = "Had a " + interactionTypeToString(type) + " with " + b.id;
                obs.firsthand = true;
                a.worldView->addObservation(obs);
            }
            if (b.worldView) {
                Observation obs;
                obs.eventId = "social_" + b.id + "_" + a.id;
                obs.description = "Had a " + interactionTypeToString(type) + " with " + a.id;
                obs.firsthand = true;
                b.worldView->addObservation(obs);
            }

            setCooldown(a.id, b.id);
            results.push_back(result);
        }
    }

    return results;
}

// ============================================================================
// Interaction type selection
// ============================================================================

InteractionType SocialInteractionSystem::chooseInteraction(float disposition, float socialNeedUrgency) {
    // Very negative disposition → hostile
    if (disposition < -1.5f) return InteractionType::Insult;
    if (disposition < -0.5f) return InteractionType::Insult;

    // Neutral to slightly positive
    if (disposition < 0.5f) {
        // High social urgency drives more engaged interaction
        if (socialNeedUrgency > 0.7f) return InteractionType::Conversation;
        return InteractionType::Greeting;
    }

    // Positive disposition
    if (disposition < 1.5f) {
        if (socialNeedUrgency > 0.5f) return InteractionType::Gossip;
        return InteractionType::Conversation;
    }

    // Very positive → deep conversation with gossip
    return InteractionType::Gossip;
}

// ============================================================================
// Gossip sharing
// ============================================================================

void SocialInteractionSystem::shareGossip(const WorldView& sharer, WorldView& listener,
                                            float gameTime) {
    // Share beliefs the listener doesn't have
    const auto& beliefs = sharer.getAllBeliefs();
    for (const auto& [key, belief] : beliefs) {
        if (!listener.hasBelief(key)) {
            // Secondhand belief has reduced confidence
            listener.setBelief(key, belief.value, belief.confidence * 0.6f, gameTime);
            return; // One piece per interaction
        }
    }

    // Share observations the listener hasn't seen
    auto recent = sharer.getRecentObservations(5);
    for (const auto* obs : recent) {
        if (obs->firsthand) {
            // Create secondhand copy
            Observation copy = *obs;
            copy.firsthand = false;
            listener.addObservation(copy);
            return; // One piece per interaction
        }
    }
}

} // namespace AI
} // namespace Phyxel
