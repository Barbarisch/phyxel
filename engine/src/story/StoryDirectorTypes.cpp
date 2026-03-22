#include "story/StoryDirectorTypes.h"
#include <stdexcept>

namespace Phyxel {
namespace Story {

// ============================================================================
// BeatType conversion
// ============================================================================

std::string beatTypeToString(BeatType type) {
    switch (type) {
        case BeatType::Hard:     return "Hard";
        case BeatType::Soft:     return "Soft";
        case BeatType::Optional: return "Optional";
    }
    return "Soft";
}

BeatType beatTypeFromString(const std::string& str) {
    if (str == "Hard")     return BeatType::Hard;
    if (str == "Optional") return BeatType::Optional;
    return BeatType::Soft;
}

// ============================================================================
// ArcConstraintMode conversion
// ============================================================================

std::string arcConstraintModeToString(ArcConstraintMode mode) {
    switch (mode) {
        case ArcConstraintMode::Scripted:  return "Scripted";
        case ArcConstraintMode::Guided:    return "Guided";
        case ArcConstraintMode::Emergent:  return "Emergent";
        case ArcConstraintMode::Freeform:  return "Freeform";
    }
    return "Guided";
}

ArcConstraintMode arcConstraintModeFromString(const std::string& str) {
    if (str == "Scripted")  return ArcConstraintMode::Scripted;
    if (str == "Emergent")  return ArcConstraintMode::Emergent;
    if (str == "Freeform")  return ArcConstraintMode::Freeform;
    return ArcConstraintMode::Guided;
}

// ============================================================================
// BeatStatus conversion
// ============================================================================

std::string beatStatusToString(BeatStatus status) {
    switch (status) {
        case BeatStatus::Pending:   return "Pending";
        case BeatStatus::Active:    return "Active";
        case BeatStatus::Completed: return "Completed";
        case BeatStatus::Skipped:   return "Skipped";
        case BeatStatus::Failed:    return "Failed";
    }
    return "Pending";
}

BeatStatus beatStatusFromString(const std::string& str) {
    if (str == "Active")    return BeatStatus::Active;
    if (str == "Completed") return BeatStatus::Completed;
    if (str == "Skipped")   return BeatStatus::Skipped;
    if (str == "Failed")    return BeatStatus::Failed;
    return BeatStatus::Pending;
}

// ============================================================================
// StoryBeat helpers
// ============================================================================

void to_json(nlohmann::json& j, const StoryBeat& b) {
    j = nlohmann::json{
        {"id", b.id},
        {"description", b.description},
        {"type", beatTypeToString(b.type)},
        {"triggerCondition", b.triggerCondition},
        {"directorActions", b.directorActions},
        {"requiredCharacters", b.requiredCharacters},
        {"prerequisites", b.prerequisites},
        {"completionCondition", b.completionCondition},
        {"failureCondition", b.failureCondition},
        {"status", beatStatusToString(b.status)},
        {"activatedTime", b.activatedTime},
        {"completedTime", b.completedTime}
    };
}

void from_json(const nlohmann::json& j, StoryBeat& b) {
    j.at("id").get_to(b.id);
    if (j.contains("description")) j.at("description").get_to(b.description);
    if (j.contains("type")) b.type = beatTypeFromString(j.at("type").get<std::string>());
    if (j.contains("triggerCondition")) j.at("triggerCondition").get_to(b.triggerCondition);
    if (j.contains("directorActions")) j.at("directorActions").get_to(b.directorActions);
    if (j.contains("requiredCharacters")) j.at("requiredCharacters").get_to(b.requiredCharacters);
    if (j.contains("prerequisites")) j.at("prerequisites").get_to(b.prerequisites);
    if (j.contains("completionCondition")) j.at("completionCondition").get_to(b.completionCondition);
    if (j.contains("failureCondition")) j.at("failureCondition").get_to(b.failureCondition);
    if (j.contains("status")) b.status = beatStatusFromString(j.at("status").get<std::string>());
    if (j.contains("activatedTime")) j.at("activatedTime").get_to(b.activatedTime);
    if (j.contains("completedTime")) j.at("completedTime").get_to(b.completedTime);
}

// ============================================================================
// StoryArc helpers
// ============================================================================

int StoryArc::completedBeatCount() const {
    int count = 0;
    for (auto& b : beats)
        if (b.status == BeatStatus::Completed) ++count;
    return count;
}

int StoryArc::totalBeatCount() const {
    return static_cast<int>(beats.size());
}

const StoryBeat* StoryArc::getBeat(const std::string& beatId) const {
    for (auto& b : beats)
        if (b.id == beatId) return &b;
    return nullptr;
}

StoryBeat* StoryArc::getBeatMut(const std::string& beatId) {
    for (auto& b : beats)
        if (b.id == beatId) return &b;
    return nullptr;
}

void StoryArc::recalculateProgress() {
    if (beats.empty()) {
        progress = 0.0f;
        return;
    }
    progress = static_cast<float>(completedBeatCount()) / static_cast<float>(beats.size());
    isCompleted = (completedBeatCount() == totalBeatCount());
}

void to_json(nlohmann::json& j, const StoryArc& a) {
    j = nlohmann::json{
        {"id", a.id},
        {"name", a.name},
        {"description", a.description},
        {"constraintMode", arcConstraintModeToString(a.constraintMode)},
        {"minTimeBetweenBeats", a.minTimeBetweenBeats},
        {"maxTimeWithoutProgress", a.maxTimeWithoutProgress},
        {"tensionCurve", a.tensionCurve},
        {"isActive", a.isActive},
        {"isCompleted", a.isCompleted},
        {"progress", a.progress},
        {"lastBeatTime", a.lastBeatTime}
    };

    j["beats"] = nlohmann::json::array();
    for (auto& b : a.beats)
        j["beats"].push_back(b);
}

void from_json(const nlohmann::json& j, StoryArc& a) {
    j.at("id").get_to(a.id);
    if (j.contains("name")) j.at("name").get_to(a.name);
    if (j.contains("description")) j.at("description").get_to(a.description);
    if (j.contains("constraintMode"))
        a.constraintMode = arcConstraintModeFromString(j.at("constraintMode").get<std::string>());
    if (j.contains("minTimeBetweenBeats")) j.at("minTimeBetweenBeats").get_to(a.minTimeBetweenBeats);
    if (j.contains("maxTimeWithoutProgress")) j.at("maxTimeWithoutProgress").get_to(a.maxTimeWithoutProgress);
    if (j.contains("tensionCurve")) j.at("tensionCurve").get_to(a.tensionCurve);
    if (j.contains("isActive")) j.at("isActive").get_to(a.isActive);
    if (j.contains("isCompleted")) j.at("isCompleted").get_to(a.isCompleted);
    if (j.contains("progress")) j.at("progress").get_to(a.progress);
    if (j.contains("lastBeatTime")) j.at("lastBeatTime").get_to(a.lastBeatTime);

    if (j.contains("beats")) {
        a.beats.clear();
        for (auto& bj : j["beats"])
            a.beats.push_back(bj.get<StoryBeat>());
    }
}

// ============================================================================
// DirectorAction
// ============================================================================

void to_json(nlohmann::json& j, const DirectorAction& a) {
    j = nlohmann::json{{"type", a.type}, {"params", a.params}};
}

void from_json(const nlohmann::json& j, DirectorAction& a) {
    j.at("type").get_to(a.type);
    if (j.contains("params")) a.params = j["params"];
}

} // namespace Story
} // namespace Phyxel
