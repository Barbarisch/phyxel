#include "story/KnowledgePropagator.h"
#include <algorithm>
#include <cmath>
#include <functional>

namespace Phyxel {
namespace Story {

// ============================================================================
// Utilities
// ============================================================================

size_t KnowledgePropagator::hashCombine(size_t a, size_t b) {
    return a ^ (b + 0x9e3779b9 + (a << 6) + (a >> 2));
}

// ============================================================================
// Channel 1: Witnessing
// ============================================================================

std::vector<std::string> KnowledgePropagator::findWitnesses(
    const WorldEvent& event,
    const std::unordered_map<std::string, CharacterProfile>& characters) {

    std::vector<std::string> witnesses;
    if (!m_positionLookup) return witnesses;

    float maxRadius = std::max(event.visibleRadius, event.audibleRadius);
    if (maxRadius <= 0.0f) return witnesses;

    for (auto& [charId, profile] : characters) {
        // Participants always witness the event
        bool isParticipant = std::find(event.participants.begin(), event.participants.end(), charId)
                             != event.participants.end();
        if (isParticipant) {
            witnesses.push_back(charId);
            continue;
        }

        glm::vec3 charPos;
        if (!m_positionLookup(charId, charPos)) continue;

        float dist = glm::distance(event.location, charPos);
        if (dist <= maxRadius) {
            witnesses.push_back(charId);
        }
    }

    return witnesses;
}

std::vector<std::string> KnowledgePropagator::propagateWitness(
    const WorldEvent& event,
    const std::unordered_map<std::string, CharacterProfile>& characters,
    std::unordered_map<std::string, CharacterMemory>& memories) {

    auto witnesses = findWitnesses(event, characters);

    for (auto& charId : witnesses) {
        auto charIt = characters.find(charId);
        auto memIt = memories.find(charId);
        if (charIt == characters.end() || memIt == memories.end()) continue;

        memIt->second.witness(event, charIt->second.traits);
    }

    return witnesses;
}

// ============================================================================
// Channel 2: Telling (during dialogue)
// ============================================================================

std::vector<const KnowledgeFact*> KnowledgePropagator::selectFactsToShare(
    const CharacterProfile& speakerProfile,
    const CharacterProfile& listenerProfile,
    const CharacterMemory& speakerMemory,
    int maxFacts) {

    // Check trust threshold — speaker won't share with someone they deeply distrust
    auto* rel = speakerProfile.getRelationship(listenerProfile.id);
    float trust = rel ? rel->trust : 0.0f;
    if (trust < m_dialogueTrustThreshold) return {};

    // Gather all shareable facts
    struct ScoredFact {
        const KnowledgeFact* fact;
        float score;
    };
    std::vector<ScoredFact> candidates;

    for (auto& [factId, fact] : speakerMemory.getAllFacts()) {
        // Only share facts the speaker is reasonably confident about
        if (fact.confidence < 0.3f) continue;

        // Score: importance * confidence * extraversion boost
        float score = 1.0f;

        // Confidence matters
        score *= fact.confidence;

        // Extroverted characters share more freely
        score *= (0.5f + 0.5f * speakerProfile.traits.extraversion);

        // Higher trust = more willing to share sensitive info
        score *= (0.5f + 0.5f * std::max(0.0f, trust));

        candidates.push_back({&fact, score});
    }

    // Sort by score descending
    std::sort(candidates.begin(), candidates.end(),
              [](const ScoredFact& a, const ScoredFact& b) { return a.score > b.score; });

    // Take top N
    std::vector<const KnowledgeFact*> result;
    int count = std::min(maxFacts, static_cast<int>(candidates.size()));
    for (int i = 0; i < count; ++i) {
        result.push_back(candidates[i].fact);
    }
    return result;
}

int KnowledgePropagator::propagateDialogue(
    const std::string& speakerId,
    const std::string& listenerId,
    const CharacterProfile& speakerProfile,
    const CharacterProfile& listenerProfile,
    const CharacterMemory& speakerMemory,
    CharacterMemory& listenerMemory,
    int maxFacts) {

    auto factsToShare = selectFactsToShare(speakerProfile, listenerProfile, speakerMemory, maxFacts);

    int shared = 0;
    for (auto* fact : factsToShare) {
        // Don't tell them what they already know with higher confidence
        if (listenerMemory.knowsAbout(fact->factId)) {
            auto* existing = listenerMemory.getFact(fact->factId);
            if (existing && existing->confidence >= fact->confidence)
                continue;
        }

        listenerMemory.hearFrom(speakerId, *fact, listenerProfile.traits);
        ++shared;
    }

    return shared;
}

// ============================================================================
// Channel 3: Rumors (background spread)
// ============================================================================

void KnowledgePropagator::propagateRumors(
    float dt,
    float proximityRadius,
    const std::unordered_map<std::string, CharacterProfile>& characters,
    std::unordered_map<std::string, CharacterMemory>& memories) {

    if (!m_positionLookup) return;

    // Build position cache
    struct CharPos {
        std::string id;
        glm::vec3 position;
    };
    std::vector<CharPos> positioned;
    positioned.reserve(characters.size());

    for (auto& [charId, profile] : characters) {
        glm::vec3 pos;
        if (m_positionLookup(charId, pos)) {
            positioned.push_back({charId, pos});
        }
    }

    // For each pair of nearby characters, potentially spread rumors
    for (size_t i = 0; i < positioned.size(); ++i) {
        for (size_t j = i + 1; j < positioned.size(); ++j) {
            float dist = glm::distance(positioned[i].position, positioned[j].position);
            if (dist > proximityRadius) continue;

            auto charItA = characters.find(positioned[i].id);
            auto charItB = characters.find(positioned[j].id);
            if (charItA == characters.end() || charItB == characters.end()) continue;

            auto memItA = memories.find(positioned[i].id);
            auto memItB = memories.find(positioned[j].id);
            if (memItA == memories.end() || memItB == memories.end()) continue;

            // Chance based on extraversion and dt
            float chanceAtoB = m_rumorBaseChance * charItA->second.traits.extraversion * dt;
            float chanceBtoA = m_rumorBaseChance * charItB->second.traits.extraversion * dt;

            // A spreads to B
            if (chanceAtoB > 0.0f) {
                spreadRumorBetween(charItA->second, charItB->second,
                                   memItA->second, memItB->second, chanceAtoB);
            }

            // B spreads to A
            if (chanceBtoA > 0.0f) {
                spreadRumorBetween(charItB->second, charItA->second,
                                   memItB->second, memItA->second, chanceBtoA);
            }
        }
    }
}

// ============================================================================
// Rumor helper
// ============================================================================

void KnowledgePropagator::spreadRumorBetween(
    const CharacterProfile& speakerProfile,
    const CharacterProfile& listenerProfile,
    const CharacterMemory& speakerMemory,
    CharacterMemory& listenerMemory,
    float chance) {

    // Use a deterministic selection based on character IDs to avoid needing RNG state
    // This means the same pair of characters will pick the same fact to share,
    // which is fine — it simulates a consistent conversation topic
    auto& allFacts = speakerMemory.getAllFacts();
    if (allFacts.empty()) return;

    // Find the best rumor candidate: high importance, high confidence, not known by listener
    const KnowledgeFact* bestFact = nullptr;
    float bestScore = -1.0f;

    for (auto& [factId, fact] : allFacts) {
        // Skip low-confidence facts
        if (fact.confidence < 0.2f) continue;

        // Skip innate knowledge (not gossip-worthy)
        if (fact.source == KnowledgeSource::Innate) continue;

        // Skip if listener already knows with equal or higher confidence
        if (listenerMemory.knowsAbout(factId)) {
            auto* existing = listenerMemory.getFact(factId);
            if (existing && existing->confidence >= fact.confidence * 0.5f)
                continue;
        }

        float score = fact.confidence;

        // Use hash of IDs to add deterministic variety
        size_t h = hashCombine(std::hash<std::string>{}(speakerProfile.id),
                               std::hash<std::string>{}(factId));
        float variety = static_cast<float>(h % 100) / 100.0f;
        score += variety * 0.2f;

        if (score > bestScore) {
            bestScore = score;
            bestFact = &fact;
        }
    }

    if (!bestFact) return;

    // Chance gate: the actual probability accumulated from dt * base * extraversion
    // We use the hash to make this deterministic per-pair-per-fact
    size_t pairHash = hashCombine(std::hash<std::string>{}(speakerProfile.id),
                                  std::hash<std::string>{}(listenerProfile.id));
    float threshold = static_cast<float>(pairHash % 1000) / 1000.0f;

    if (chance < threshold) return;

    // Spread as rumor with reduced confidence
    KnowledgeFact rumor;
    rumor.factId = bestFact->factId;
    rumor.summary = bestFact->summary;
    rumor.source = KnowledgeSource::Rumor;
    rumor.sourceCharacterId = speakerProfile.id;
    rumor.confidence = bestFact->confidence * 0.5f;
    rumor.timestamp = bestFact->timestamp;
    rumor.perceivedDetails = bestFact->perceivedDetails;

    listenerMemory.hearFrom(speakerProfile.id, rumor, listenerProfile.traits);
}

} // namespace Story
} // namespace Phyxel
