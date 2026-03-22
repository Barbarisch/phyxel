#include "story/StoryEngine.h"

namespace Phyxel {
namespace Story {

// ============================================================================
// World Setup
// ============================================================================

void StoryEngine::defineWorld(WorldState state) {
    m_worldState = std::move(state);
}

void StoryEngine::addCharacter(CharacterProfile profile) {
    std::string id = profile.id;

    // Add to faction member list if applicable
    if (!profile.factionId.empty()) {
        auto* faction = const_cast<Faction*>(m_worldState.getFaction(profile.factionId));
        if (faction) {
            auto& members = faction->memberCharacterIds;
            if (std::find(members.begin(), members.end(), id) == members.end())
                members.push_back(id);
        }
    }

    m_characters[id] = std::move(profile);

    // Create an empty memory if none exists
    if (m_memories.find(id) == m_memories.end())
        m_memories[id] = CharacterMemory{};
}

bool StoryEngine::removeCharacter(const std::string& characterId) {
    auto it = m_characters.find(characterId);
    if (it == m_characters.end()) return false;

    // Remove from faction
    if (!it->second.factionId.empty()) {
        auto* faction = const_cast<Faction*>(m_worldState.getFaction(it->second.factionId));
        if (faction) {
            auto& members = faction->memberCharacterIds;
            members.erase(std::remove(members.begin(), members.end(), characterId), members.end());
        }
    }

    m_characters.erase(it);
    m_memories.erase(characterId);
    return true;
}

const CharacterProfile* StoryEngine::getCharacter(const std::string& id) const {
    auto it = m_characters.find(id);
    return (it != m_characters.end()) ? &it->second : nullptr;
}

CharacterProfile* StoryEngine::getCharacterMut(const std::string& id) {
    auto it = m_characters.find(id);
    return (it != m_characters.end()) ? &it->second : nullptr;
}

std::vector<std::string> StoryEngine::getCharacterIds() const {
    std::vector<std::string> ids;
    ids.reserve(m_characters.size());
    for (auto& [id, _] : m_characters)
        ids.push_back(id);
    return ids;
}

void StoryEngine::addStartingKnowledge(const std::string& characterId,
                                        const std::string& factId,
                                        const std::string& summary,
                                        const nlohmann::json& details) {
    auto it = m_memories.find(characterId);
    if (it == m_memories.end()) return;
    it->second.addInnateKnowledge(factId, summary, details);
}

const CharacterMemory* StoryEngine::getCharacterMemory(const std::string& characterId) const {
    auto it = m_memories.find(characterId);
    return (it != m_memories.end()) ? &it->second : nullptr;
}

CharacterMemory* StoryEngine::getCharacterMemoryMut(const std::string& characterId) {
    auto it = m_memories.find(characterId);
    return (it != m_memories.end()) ? &it->second : nullptr;
}

// ============================================================================
// Runtime
// ============================================================================

void StoryEngine::update(float dt) {
    m_worldState.worldTime += dt;

    // Update emotion decay for all characters
    for (auto& [id, profile] : m_characters) {
        profile.emotion.decay(dt, profile.traits);
    }

    // Update memory decay for all characters
    for (auto& [id, memory] : m_memories) {
        memory.update(dt);
    }

    // Update story director (evaluate beats, manage pacing)
    m_director.update(dt, m_worldState);
}

void StoryEngine::triggerEvent(WorldEvent event) {
    m_worldState.recordEvent(event);

    // Broadcast via event bus
    m_eventBus.emit(event);

    // Propagate to witnesses — characters near the event learn about it
    m_propagator.propagateWitness(event, m_characters, m_memories);
}

int StoryEngine::shareKnowledge(const std::string& speakerId, const std::string& listenerId, int maxFacts) {
    auto speakerIt = m_characters.find(speakerId);
    auto listenerIt = m_characters.find(listenerId);
    if (speakerIt == m_characters.end() || listenerIt == m_characters.end()) return 0;

    auto speakerMemIt = m_memories.find(speakerId);
    auto listenerMemIt = m_memories.find(listenerId);
    if (speakerMemIt == m_memories.end() || listenerMemIt == m_memories.end()) return 0;

    return m_propagator.propagateDialogue(
        speakerId, listenerId,
        speakerIt->second, listenerIt->second,
        speakerMemIt->second, listenerMemIt->second,
        maxFacts);
}

void StoryEngine::spreadRumors(float dt, float proximityRadius) {
    m_propagator.propagateRumors(dt, proximityRadius, m_characters, m_memories);
}

void StoryEngine::addStoryArc(StoryArc arc, bool activate) {
    std::string id = arc.id;
    if (activate) arc.isActive = true;
    m_director.addArc(std::move(arc));
    // Ensure director is listening to our event bus
    m_director.listenTo(m_eventBus);
}

const EmotionalState* StoryEngine::getCharacterEmotion(const std::string& characterId) const {
    auto* profile = getCharacter(characterId);
    return profile ? &profile->emotion : nullptr;
}

bool StoryEngine::setGoalPriority(const std::string& characterId, const std::string& goalId, float priority) {
    auto* profile = getCharacterMut(characterId);
    if (!profile) return false;
    auto* goal = profile->getGoalMut(goalId);
    if (!goal) return false;
    goal->priority = priority;
    return true;
}

bool StoryEngine::setAgencyLevel(const std::string& characterId, AgencyLevel level) {
    auto* profile = getCharacterMut(characterId);
    if (!profile) return false;
    profile->agencyLevel = level;
    return true;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json StoryEngine::saveState() const {
    nlohmann::json j;
    j["worldState"] = m_worldState;

    j["characters"] = nlohmann::json::object();
    for (auto& [id, profile] : m_characters)
        j["characters"][id] = profile;

    j["memories"] = nlohmann::json::object();
    for (auto& [id, memory] : m_memories)
        j["memories"][id] = memory.toJson();

    j["director"] = m_director.saveState();

    return j;
}

void StoryEngine::loadState(const nlohmann::json& state) {
    if (state.contains("worldState"))
        state.at("worldState").get_to(m_worldState);

    m_characters.clear();
    if (state.contains("characters")) {
        for (auto& [id, val] : state["characters"].items())
            m_characters[id] = val.get<CharacterProfile>();
    }

    m_memories.clear();
    if (state.contains("memories")) {
        for (auto& [id, val] : state["memories"].items()) {
            CharacterMemory mem;
            mem.fromJson(val);
            m_memories[id] = std::move(mem);
        }
    }

    if (state.contains("director"))
        m_director.loadState(state["director"]);
}

} // namespace Story
} // namespace Phyxel
