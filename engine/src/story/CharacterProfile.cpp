#include "story/CharacterProfile.h"
#include <cmath>
#include <algorithm>

namespace Phyxel {
namespace Story {

// ============================================================================
// AgencyLevel conversion
// ============================================================================

std::string agencyLevelToString(AgencyLevel level) {
    switch (level) {
        case AgencyLevel::Scripted:   return "scripted";
        case AgencyLevel::Templated:  return "templated";
        case AgencyLevel::Guided:     return "guided";
        case AgencyLevel::Autonomous: return "autonomous";
    }
    return "scripted";
}

AgencyLevel agencyLevelFromString(const std::string& str) {
    if (str == "templated")  return AgencyLevel::Templated;
    if (str == "guided")     return AgencyLevel::Guided;
    if (str == "autonomous") return AgencyLevel::Autonomous;
    return AgencyLevel::Scripted;
}

// ============================================================================
// PersonalityTraits
// ============================================================================

float PersonalityTraits::getTrait(const std::string& name) const {
    if (name == "openness")          return openness;
    if (name == "conscientiousness") return conscientiousness;
    if (name == "extraversion")      return extraversion;
    if (name == "agreeableness")     return agreeableness;
    if (name == "neuroticism")       return neuroticism;

    auto it = customTraits.find(name);
    return (it != customTraits.end()) ? it->second : 0.5f;
}

void to_json(nlohmann::json& j, const PersonalityTraits& t) {
    j = nlohmann::json{
        {"openness", t.openness},
        {"conscientiousness", t.conscientiousness},
        {"extraversion", t.extraversion},
        {"agreeableness", t.agreeableness},
        {"neuroticism", t.neuroticism}
    };
    if (!t.customTraits.empty())
        j["customTraits"] = t.customTraits;
}

void from_json(const nlohmann::json& j, PersonalityTraits& t) {
    t.openness          = j.value("openness", 0.5f);
    t.conscientiousness = j.value("conscientiousness", 0.5f);
    t.extraversion      = j.value("extraversion", 0.5f);
    t.agreeableness     = j.value("agreeableness", 0.5f);
    t.neuroticism       = j.value("neuroticism", 0.5f);
    t.customTraits = j.value("customTraits", std::unordered_map<std::string, float>{});
}

// ============================================================================
// EmotionalState
// ============================================================================

void EmotionalState::decay(float dt, const PersonalityTraits& personality) {
    // High neuroticism = slow decay (0.05/s at n=1), low neuroticism = fast decay (0.5/s at n=0)
    float decayRate = 0.5f - 0.45f * personality.neuroticism;
    float factor = decayRate * dt;

    // Joy decays toward 0
    if (joy > 0.0f) joy = std::max(0.0f, joy - factor);
    else if (joy < 0.0f) joy = std::min(0.0f, joy + factor);

    anger    = std::max(0.0f, anger - factor);
    fear     = std::max(0.0f, fear - factor);
    surprise = std::max(0.0f, surprise - factor);
    disgust  = std::max(0.0f, disgust - factor);
}

std::string EmotionalState::dominantEmotion() const {
    struct E { const char* name; float value; };
    E emotions[] = {
        {"joyful",    joy > 0 ? joy : 0.0f},
        {"grieving",  joy < 0 ? -joy : 0.0f},
        {"angry",     anger},
        {"afraid",    fear},
        {"surprised", surprise},
        {"disgusted", disgust}
    };

    float maxVal = 0.1f; // Threshold — below this is "neutral"
    const char* dominant = "neutral";
    for (auto& e : emotions) {
        if (e.value > maxVal) {
            maxVal = e.value;
            dominant = e.name;
        }
    }
    return dominant;
}

void to_json(nlohmann::json& j, const EmotionalState& e) {
    j = nlohmann::json{
        {"joy", e.joy}, {"anger", e.anger}, {"fear", e.fear},
        {"surprise", e.surprise}, {"disgust", e.disgust}
    };
}

void from_json(const nlohmann::json& j, EmotionalState& e) {
    e.joy      = j.value("joy", 0.0f);
    e.anger    = j.value("anger", 0.0f);
    e.fear     = j.value("fear", 0.0f);
    e.surprise = j.value("surprise", 0.0f);
    e.disgust  = j.value("disgust", 0.0f);
}

// ============================================================================
// CharacterGoal
// ============================================================================

void to_json(nlohmann::json& j, const CharacterGoal& g) {
    j = nlohmann::json{
        {"id", g.id},
        {"description", g.description},
        {"priority", g.priority},
        {"isActive", g.isActive}
    };
    if (!g.completionCondition.empty()) j["completionCondition"] = g.completionCondition;
    if (!g.failureCondition.empty())    j["failureCondition"] = g.failureCondition;
}

void from_json(const nlohmann::json& j, CharacterGoal& g) {
    g.id = j.at("id").get<std::string>();
    g.description = j.value("description", "");
    g.priority = j.value("priority", 0.5f);
    g.isActive = j.value("isActive", true);
    g.completionCondition = j.value("completionCondition", "");
    g.failureCondition = j.value("failureCondition", "");
}

// ============================================================================
// Relationship
// ============================================================================

void to_json(nlohmann::json& j, const Relationship& r) {
    j = nlohmann::json{
        {"target", r.targetCharacterId},
        {"trust", r.trust},
        {"affection", r.affection},
        {"respect", r.respect},
        {"fear", r.fear},
        {"label", r.label}
    };
}

void from_json(const nlohmann::json& j, Relationship& r) {
    r.targetCharacterId = j.at("target").get<std::string>();
    r.trust     = j.value("trust", 0.0f);
    r.affection = j.value("affection", 0.0f);
    r.respect   = j.value("respect", 0.0f);
    r.fear      = j.value("fear", 0.0f);
    r.label     = j.value("label", "");
}

// ============================================================================
// CharacterProfile
// ============================================================================

const Relationship* CharacterProfile::getRelationship(const std::string& targetId) const {
    for (auto& r : relationships) {
        if (r.targetCharacterId == targetId) return &r;
    }
    return nullptr;
}

const CharacterGoal* CharacterProfile::getGoal(const std::string& goalId) const {
    for (auto& g : goals) {
        if (g.id == goalId) return &g;
    }
    return nullptr;
}

CharacterGoal* CharacterProfile::getGoalMut(const std::string& goalId) {
    for (auto& g : goals) {
        if (g.id == goalId) return &g;
    }
    return nullptr;
}

void to_json(nlohmann::json& j, const CharacterProfile& p) {
    j = nlohmann::json{
        {"id", p.id},
        {"name", p.name},
        {"description", p.description},
        {"factionId", p.factionId},
        {"traits", p.traits},
        {"goals", p.goals},
        {"relationships", p.relationships},
        {"emotion", p.emotion},
        {"agencyLevel", agencyLevelToString(p.agencyLevel)},
        {"defaultBehavior", p.defaultBehavior},
        {"defaultDialogueFile", p.defaultDialogueFile},
        {"allowedActions", p.allowedActions},
        {"roles", p.roles}
    };
}

void from_json(const nlohmann::json& j, CharacterProfile& p) {
    p.id = j.at("id").get<std::string>();
    p.name = j.value("name", "");
    p.description = j.value("description", "");
    p.factionId = j.value("factionId", "");
    if (j.contains("traits")) j.at("traits").get_to(p.traits);
    p.goals = j.value("goals", std::vector<CharacterGoal>{});
    p.relationships = j.value("relationships", std::vector<Relationship>{});
    if (j.contains("emotion")) j.at("emotion").get_to(p.emotion);
    p.agencyLevel = agencyLevelFromString(j.value("agencyLevel", "scripted"));
    p.defaultBehavior = j.value("defaultBehavior", "");
    p.defaultDialogueFile = j.value("defaultDialogueFile", "");
    p.allowedActions = j.value("allowedActions", std::vector<std::string>{});
    p.roles = j.value("roles", std::vector<std::string>{});
}

} // namespace Story
} // namespace Phyxel
