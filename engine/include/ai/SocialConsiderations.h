#pragma once

#include "ai/UtilityAI.h"
#include "ai/NeedsSystem.h"
#include "ai/RelationshipManager.h"
#include "ai/WorldView.h"

namespace Phyxel {
namespace AI {

/// Factory functions that create UtilityAI Consideration scorers
/// driven by the social simulation subsystems (needs, relationships, worldview).
/// These capture pointers to the NPC's subsystems via lambda — the caller must
/// ensure the captured objects outlive the considerations.
namespace SocialConsiderations {

    /// Score based on how urgent a specific need is (0 = satisfied, 1 = depleted).
    inline Consideration::ScoreFunc needUrgency(NeedsSystem* needs, NeedType type) {
        return [needs, type](ActionContext&) -> float {
            if (!needs) return 0.0f;
            const Need* n = needs->getNeed(type);
            return n ? n->getUrgency() : 0.0f;
        };
    }

    /// Returns 1.0 if the given need is urgent (below threshold), 0 otherwise.
    inline Consideration::ScoreFunc needIsUrgent(NeedsSystem* needs, NeedType type) {
        return [needs, type](ActionContext&) -> float {
            if (!needs) return 0.0f;
            const Need* n = needs->getNeed(type);
            return (n && n->isUrgent()) ? 1.0f : 0.0f;
        };
    }

    /// Score based on disposition toward a specific NPC (0=hostile, 0.5=neutral, 1=friendly).
    /// Maps disposition range [-4, +4] to [0, 1].
    inline Consideration::ScoreFunc dispositionToward(RelationshipManager* rels,
                                                       const std::string& selfId,
                                                       const std::string& targetId) {
        return [rels, selfId, targetId](ActionContext&) -> float {
            if (!rels) return 0.5f;
            float d = rels->getDisposition(selfId, targetId);
            return (d + 4.0f) / 8.0f; // Map [-4,4] to [0,1]
        };
    }

    /// Score based on whether this NPC has a belief about a given key.
    inline Consideration::ScoreFunc hasBelief(WorldView* view, const std::string& beliefKey) {
        return [view, beliefKey](ActionContext&) -> float {
            if (!view) return 0.0f;
            return view->hasBelief(beliefKey) ? 1.0f : 0.0f;
        };
    }

    /// Score based on opinion sentiment toward a subject.
    /// Maps [-1, 1] to [0, 1].
    inline Consideration::ScoreFunc sentimentToward(WorldView* view, const std::string& subject) {
        return [view, subject](ActionContext&) -> float {
            if (!view) return 0.5f;
            float s = view->getSentiment(subject);
            return (s + 1.0f) / 2.0f;
        };
    }

    /// Score based on the most urgent need's urgency (any type).
    /// Useful for a "take care of needs" meta-action.
    inline Consideration::ScoreFunc anyNeedUrgency(NeedsSystem* needs) {
        return [needs](ActionContext&) -> float {
            if (!needs) return 0.0f;
            const Need* n = needs->getMostUrgent();
            return n ? n->getUrgency() : 0.0f;
        };
    }

    /// Score based on fear level toward a target (0 = no fear, 1 = max fear).
    inline Consideration::ScoreFunc fearOf(RelationshipManager* rels,
                                            const std::string& selfId,
                                            const std::string& targetId) {
        return [rels, selfId, targetId](ActionContext&) -> float {
            if (!rels) return 0.0f;
            auto rel = rels->getRelationship(selfId, targetId);
            return rel.fear;
        };
    }

    /// Score inversely proportional to safety need (high safety need = high flee score).
    inline Consideration::ScoreFunc safetyUrgency(NeedsSystem* needs) {
        return needUrgency(needs, NeedType::Safety);
    }

} // namespace SocialConsiderations

} // namespace AI
} // namespace Phyxel
