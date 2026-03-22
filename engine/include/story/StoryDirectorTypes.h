#pragma once

#include "story/StoryTypes.h"
#include <string>
#include <vector>
#include <functional>

namespace Phyxel {
namespace Story {

// ============================================================================
// BeatType — how mandatory a story beat is
// ============================================================================

enum class BeatType {
    Hard,       // Must happen. Director forces it if needed.
    Soft,       // Should happen. Director creates opportunities.
    Optional    // Nice to have. Happens if conditions align naturally.
};

std::string beatTypeToString(BeatType type);
BeatType beatTypeFromString(const std::string& str);

// ============================================================================
// ArcConstraintMode — how strongly the director steers a story arc
// ============================================================================

enum class ArcConstraintMode {
    Scripted,   // All hard beats, fixed order. Director forces events.
    Guided,     // Mix of hard + soft, flexible order. Director creates opportunities.
    Emergent,   // Soft + optional only. Director observes and injects catalysts.
    Freeform    // No beats. Director manages pacing only.
};

std::string arcConstraintModeToString(ArcConstraintMode mode);
ArcConstraintMode arcConstraintModeFromString(const std::string& str);

// ============================================================================
// BeatStatus — lifecycle of a story beat
// ============================================================================

enum class BeatStatus {
    Pending,    // Not yet triggered
    Active,     // Trigger conditions met, waiting for completion
    Completed,  // Completion condition satisfied
    Skipped,    // Passed over (soft/optional beats the player missed)
    Failed      // Failed condition met (if applicable)
};

std::string beatStatusToString(BeatStatus status);
BeatStatus beatStatusFromString(const std::string& str);

// ============================================================================
// StoryBeat — a discrete narrative event within an arc
// ============================================================================

struct StoryBeat {
    std::string id;
    std::string description;
    BeatType type = BeatType::Soft;

    // Trigger condition: a world variable key that must be truthy,
    // or a simple expression like "variable_name" checked in WorldState.
    std::string triggerCondition;

    // What the director does to advance this beat
    std::vector<std::string> directorActions;

    // Characters that must be present/available
    std::vector<std::string> requiredCharacters;

    // Ordering: beat IDs that must complete first
    std::vector<std::string> prerequisites;

    // Completion: world variable key that indicates this beat finished
    std::string completionCondition;

    // Failure: optional condition that fails this beat
    std::string failureCondition;

    // Runtime state
    BeatStatus status = BeatStatus::Pending;
    float activatedTime = 0.0f;   // World time when triggered
    float completedTime = 0.0f;   // World time when completed
};

void to_json(nlohmann::json& j, const StoryBeat& b);
void from_json(const nlohmann::json& j, StoryBeat& b);

// ============================================================================
// StoryArc — a sequence of beats forming a narrative thread
// ============================================================================

struct StoryArc {
    std::string id;
    std::string name;
    std::string description;
    ArcConstraintMode constraintMode = ArcConstraintMode::Guided;

    std::vector<StoryBeat> beats;

    // Pacing parameters (game-time seconds)
    float minTimeBetweenBeats = 60.0f;
    float maxTimeWithoutProgress = 300.0f;

    // Dramatic curve target: director tries to match this tension profile.
    // Interpolated linearly across the arc's progress (0.0 to 1.0).
    // Empty = no tension target.
    std::vector<float> tensionCurve;

    // Runtime state
    bool isActive = false;
    bool isCompleted = false;
    float progress = 0.0f;     // 0.0 to 1.0 based on completed beats
    float lastBeatTime = 0.0f; // World time of last completed beat

    // Helpers
    int completedBeatCount() const;
    int totalBeatCount() const;
    const StoryBeat* getBeat(const std::string& beatId) const;
    StoryBeat* getBeatMut(const std::string& beatId);
    void recalculateProgress();
};

void to_json(nlohmann::json& j, const StoryArc& a);
void from_json(const nlohmann::json& j, StoryArc& a);

// ============================================================================
// DirectorAction — an action the director wants to take
// ============================================================================

struct DirectorAction {
    std::string type;       // "inject_event", "promote_agency", "suggest_goal",
                            // "modify_faction", "set_variable", "skip_beat"
    nlohmann::json params;  // Action-specific parameters
};

void to_json(nlohmann::json& j, const DirectorAction& a);
void from_json(const nlohmann::json& j, DirectorAction& a);

} // namespace Story
} // namespace Phyxel
