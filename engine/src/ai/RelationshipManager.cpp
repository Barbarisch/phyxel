#include "ai/RelationshipManager.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace AI {

// ============================================================================
// InteractionType string conversions
// ============================================================================

std::string interactionTypeToString(InteractionType type) {
    switch (type) {
        case InteractionType::Greeting:     return "Greeting";
        case InteractionType::Conversation: return "Conversation";
        case InteractionType::Trade:        return "Trade";
        case InteractionType::Gift:         return "Gift";
        case InteractionType::Insult:       return "Insult";
        case InteractionType::Attack:       return "Attack";
        case InteractionType::Help:         return "Help";
        case InteractionType::Betray:       return "Betray";
        case InteractionType::Gossip:       return "Gossip";
        case InteractionType::Witnessed:    return "Witnessed";
    }
    return "Greeting";
}

InteractionType interactionTypeFromString(const std::string& str) {
    if (str == "Greeting")     return InteractionType::Greeting;
    if (str == "Conversation") return InteractionType::Conversation;
    if (str == "Trade")        return InteractionType::Trade;
    if (str == "Gift")         return InteractionType::Gift;
    if (str == "Insult")       return InteractionType::Insult;
    if (str == "Attack")       return InteractionType::Attack;
    if (str == "Help")         return InteractionType::Help;
    if (str == "Betray")       return InteractionType::Betray;
    if (str == "Gossip")       return InteractionType::Gossip;
    if (str == "Witnessed")    return InteractionType::Witnessed;
    return InteractionType::Greeting;
}

// ============================================================================
// RelationshipManager
// ============================================================================

Story::Relationship RelationshipManager::getRelationship(const std::string& fromId, const std::string& toId) const {
    auto it = m_relationships.find(makeKey(fromId, toId));
    if (it != m_relationships.end()) return it->second;

    Story::Relationship neutral;
    neutral.targetCharacterId = toId;
    return neutral;
}

void RelationshipManager::setRelationship(const std::string& fromId, const std::string& toId,
                                           const Story::Relationship& rel) {
    Story::Relationship r = rel;
    r.targetCharacterId = toId;
    m_relationships[makeKey(fromId, toId)] = r;
}

bool RelationshipManager::hasRelationship(const std::string& fromId, const std::string& toId) const {
    return m_relationships.count(makeKey(fromId, toId)) > 0;
}

std::vector<std::pair<std::string, Story::Relationship>>
RelationshipManager::getRelationshipsFor(const std::string& characterId) const {
    std::vector<std::pair<std::string, Story::Relationship>> result;
    std::string prefix = characterId + "->";
    for (const auto& [key, rel] : m_relationships) {
        if (key.substr(0, prefix.size()) == prefix) {
            result.emplace_back(rel.targetCharacterId, rel);
        }
    }
    return result;
}

std::vector<std::pair<std::string, Story::Relationship>>
RelationshipManager::getRelationshipsAbout(const std::string& targetId) const {
    std::vector<std::pair<std::string, Story::Relationship>> result;
    std::string suffix = "->" + targetId;
    for (const auto& [key, rel] : m_relationships) {
        if (key.size() >= suffix.size() &&
            key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
            std::string fromId = key.substr(0, key.size() - suffix.size());
            result.emplace_back(fromId, rel);
        }
    }
    return result;
}

void RelationshipManager::applyInteraction(const std::string& fromId, const std::string& toId,
                                            InteractionType type, float intensity) {
    auto key = makeKey(fromId, toId);
    auto it = m_relationships.find(key);
    if (it == m_relationships.end()) {
        Story::Relationship neutral;
        neutral.targetCharacterId = toId;
        m_relationships[key] = neutral;
        it = m_relationships.find(key);
    }

    auto& rel = it->second;
    float i = std::clamp(intensity, 0.0f, 2.0f);

    switch (type) {
        case InteractionType::Greeting:
            rel.trust += 0.02f * i;
            rel.affection += 0.01f * i;
            break;
        case InteractionType::Conversation:
            rel.trust += 0.05f * i;
            rel.affection += 0.03f * i;
            rel.respect += 0.02f * i;
            break;
        case InteractionType::Trade:
            rel.trust += 0.08f * i;
            rel.respect += 0.03f * i;
            break;
        case InteractionType::Gift:
            rel.affection += 0.10f * i;
            rel.trust += 0.05f * i;
            break;
        case InteractionType::Insult:
            rel.affection -= 0.10f * i;
            rel.respect -= 0.08f * i;
            rel.trust -= 0.05f * i;
            break;
        case InteractionType::Attack:
            rel.trust -= 0.30f * i;
            rel.affection -= 0.25f * i;
            rel.fear += 0.20f * i;
            break;
        case InteractionType::Help:
            rel.trust += 0.15f * i;
            rel.affection += 0.10f * i;
            rel.respect += 0.08f * i;
            rel.fear = std::max(0.0f, rel.fear - 0.05f * i);
            break;
        case InteractionType::Betray:
            rel.trust -= 0.40f * i;
            rel.affection -= 0.20f * i;
            rel.respect -= 0.15f * i;
            break;
        case InteractionType::Gossip:
            rel.trust += 0.03f * i;  // Sharing info = slight trust
            break;
        case InteractionType::Witnessed:
            // Effect depends on context, handled externally
            break;
    }

    // Clamp all values
    rel.trust     = std::clamp(rel.trust, -1.0f, 1.0f);
    rel.affection = std::clamp(rel.affection, -1.0f, 1.0f);
    rel.respect   = std::clamp(rel.respect, -1.0f, 1.0f);
    rel.fear      = std::clamp(rel.fear, 0.0f, 1.0f);

    notifyChange(fromId, toId, rel, type);
}

void RelationshipManager::applyFactionReputation(const std::string& actorId,
                                                   const std::string& factionId,
                                                   float trustDelta,
                                                   const std::vector<std::string>& factionMembers) {
    (void)factionId;
    for (const auto& memberId : factionMembers) {
        if (memberId == actorId) continue;

        auto key = makeKey(memberId, actorId);
        auto it = m_relationships.find(key);
        if (it == m_relationships.end()) {
            Story::Relationship neutral;
            neutral.targetCharacterId = actorId;
            m_relationships[key] = neutral;
            it = m_relationships.find(key);
        }

        // Faction members are influenced at half strength
        it->second.trust = std::clamp(it->second.trust + trustDelta * 0.5f, -1.0f, 1.0f);
    }
}

void RelationshipManager::update(float deltaHours) {
    for (auto& [key, rel] : m_relationships) {
        // Drift toward neutral (0) over time
        auto decay = [&](float& val) {
            if (val > 0.0f) val = std::max(0.0f, val - m_decayRate * deltaHours);
            else if (val < 0.0f) val = std::min(0.0f, val + m_decayRate * deltaHours);
        };
        decay(rel.trust);
        decay(rel.affection);
        decay(rel.respect);
        // Fear decays slightly faster
        if (rel.fear > 0.0f) rel.fear = std::max(0.0f, rel.fear - m_decayRate * 1.5f * deltaHours);
    }
}

void RelationshipManager::onRelationshipChanged(RelationshipChangeCallback callback) {
    m_callbacks.push_back(std::move(callback));
}

void RelationshipManager::removeCharacter(const std::string& characterId) {
    std::string prefix = characterId + "->";
    std::string suffix = "->" + characterId;

    for (auto it = m_relationships.begin(); it != m_relationships.end(); ) {
        if (it->first.substr(0, prefix.size()) == prefix ||
            (it->first.size() >= suffix.size() &&
             it->first.compare(it->first.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            it = m_relationships.erase(it);
        } else {
            ++it;
        }
    }
}

float RelationshipManager::getDisposition(const std::string& fromId, const std::string& toId) const {
    auto rel = getRelationship(fromId, toId);
    return rel.trust + rel.affection + rel.respect - rel.fear;
}

size_t RelationshipManager::size() const {
    return m_relationships.size();
}

void RelationshipManager::notifyChange(const std::string& fromId, const std::string& toId,
                                        const Story::Relationship& rel, InteractionType cause) {
    for (auto& cb : m_callbacks) {
        cb(fromId, toId, rel, cause);
    }
}

nlohmann::json RelationshipManager::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [key, rel] : m_relationships) {
        auto arrow = key.find("->");
        nlohmann::json entry;
        entry["from"] = key.substr(0, arrow);
        entry["to"] = key.substr(arrow + 2);
        entry["trust"] = rel.trust;
        entry["affection"] = rel.affection;
        entry["respect"] = rel.respect;
        entry["fear"] = rel.fear;
        entry["label"] = rel.label;
        arr.push_back(entry);
    }
    return arr;
}

void RelationshipManager::fromJson(const nlohmann::json& j) {
    m_relationships.clear();
    if (!j.is_array()) return;
    for (const auto& item : j) {
        std::string from = item.value("from", "");
        std::string to = item.value("to", "");
        if (from.empty() || to.empty()) continue;

        Story::Relationship rel;
        rel.targetCharacterId = to;
        rel.trust = item.value("trust", 0.0f);
        rel.affection = item.value("affection", 0.0f);
        rel.respect = item.value("respect", 0.0f);
        rel.fear = item.value("fear", 0.0f);
        rel.label = item.value("label", "");
        m_relationships[makeKey(from, to)] = rel;
    }
}

} // namespace AI
} // namespace Phyxel
