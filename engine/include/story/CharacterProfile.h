#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Story {

// ============================================================================
// AgencyLevel — how much AI drives this character's behavior
// ============================================================================

enum class AgencyLevel {
    Scripted  = 0,  // Fixed behavior + dialogue trees
    Templated = 1,  // Fixed behavior, AI-flavored dialogue
    Guided    = 2,  // AI chooses from allowed actions
    Autonomous = 3  // AI drives everything from goals + personality
};

std::string agencyLevelToString(AgencyLevel level);
AgencyLevel agencyLevelFromString(const std::string& str);

// ============================================================================
// PersonalityTraits — Big Five + custom game-specific traits
// ============================================================================

struct PersonalityTraits {
    // Big Five personality model (each 0.0 to 1.0)
    float openness = 0.5f;
    float conscientiousness = 0.5f;
    float extraversion = 0.5f;
    float agreeableness = 0.5f;
    float neuroticism = 0.5f;

    // Game-specific traits (developer-defined), e.g. "bravery": 0.8
    std::unordered_map<std::string, float> customTraits;

    float getTrait(const std::string& name) const;
};

void to_json(nlohmann::json& j, const PersonalityTraits& t);
void from_json(const nlohmann::json& j, PersonalityTraits& t);

// ============================================================================
// EmotionalState — current emotional state, decays over time
// ============================================================================

struct EmotionalState {
    float joy = 0.0f;       // -1 (grief) to 1 (elation)
    float anger = 0.0f;     // 0 to 1
    float fear = 0.0f;      // 0 to 1
    float surprise = 0.0f;  // 0 to 1
    float disgust = 0.0f;   // 0 to 1

    /// Emotions decay toward baseline over time.
    /// High neuroticism = slow decay (lingers). Low neuroticism = fast recovery.
    void decay(float dt, const PersonalityTraits& personality);

    /// Returns the dominant emotion as a string (for AI context / speech bubble)
    std::string dominantEmotion() const;
};

void to_json(nlohmann::json& j, const EmotionalState& e);
void from_json(const nlohmann::json& j, EmotionalState& e);

// ============================================================================
// CharacterGoal — something this character wants to achieve
// ============================================================================

struct CharacterGoal {
    std::string id;
    std::string description;
    float priority = 0.5f;    // 0.0 to 1.0
    bool isActive = true;

    std::string completionCondition;
    std::string failureCondition;
};

void to_json(nlohmann::json& j, const CharacterGoal& g);
void from_json(const nlohmann::json& j, CharacterGoal& g);

// ============================================================================
// Relationship — how this character feels about another
// ============================================================================

struct Relationship {
    std::string targetCharacterId;
    float trust = 0.0f;       // -1 to 1
    float affection = 0.0f;   // -1 to 1
    float respect = 0.0f;     // -1 to 1
    float fear = 0.0f;        // 0 to 1
    std::string label;         // "friend", "rival", "mentor", etc.
};

void to_json(nlohmann::json& j, const Relationship& r);
void from_json(const nlohmann::json& j, Relationship& r);

// ============================================================================
// CharacterProfile — everything that defines who a character is
// ============================================================================

struct CharacterProfile {
    std::string id;
    std::string name;
    std::string description;
    std::string factionId;

    PersonalityTraits traits;
    std::vector<CharacterGoal> goals;
    std::vector<Relationship> relationships;
    EmotionalState emotion;

    AgencyLevel agencyLevel = AgencyLevel::Scripted;

    // Level 0-1 fallbacks
    std::string defaultBehavior;       // "idle", "patrol", etc.
    std::string defaultDialogueFile;   // "guard_intro.json"

    // Level 2+ guardrails (empty = unrestricted)
    std::vector<std::string> allowedActions;

    // Story Director tags
    std::vector<std::string> roles;    // "questgiver", "villain", "merchant", etc.

    // --- Query helpers ---
    const Relationship* getRelationship(const std::string& targetId) const;
    const CharacterGoal* getGoal(const std::string& goalId) const;
    CharacterGoal* getGoalMut(const std::string& goalId);
};

void to_json(nlohmann::json& j, const CharacterProfile& p);
void from_json(const nlohmann::json& j, CharacterProfile& p);

} // namespace Story
} // namespace Phyxel
