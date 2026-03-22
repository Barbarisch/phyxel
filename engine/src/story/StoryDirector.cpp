#include "story/StoryDirector.h"
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Story {

// ============================================================================
// Arc Management
// ============================================================================

void StoryDirector::addArc(StoryArc arc) {
    // Replace if already exists
    for (auto& existing : m_arcs) {
        if (existing.id == arc.id) {
            existing = std::move(arc);
            return;
        }
    }
    m_arcs.push_back(std::move(arc));
}

bool StoryDirector::removeArc(const std::string& arcId) {
    auto it = std::find_if(m_arcs.begin(), m_arcs.end(),
                           [&](const StoryArc& a) { return a.id == arcId; });
    if (it == m_arcs.end()) return false;
    m_arcs.erase(it);
    return true;
}

void StoryDirector::activateArc(const std::string& arcId) {
    for (auto& arc : m_arcs) {
        if (arc.id == arcId) {
            arc.isActive = true;
            return;
        }
    }
}

void StoryDirector::deactivateArc(const std::string& arcId) {
    for (auto& arc : m_arcs) {
        if (arc.id == arcId) {
            arc.isActive = false;
            return;
        }
    }
}

const StoryArc* StoryDirector::getArc(const std::string& arcId) const {
    for (auto& arc : m_arcs)
        if (arc.id == arcId) return &arc;
    return nullptr;
}

StoryArc* StoryDirector::getArcMut(const std::string& arcId) {
    for (auto& arc : m_arcs)
        if (arc.id == arcId) return &arc;
    return nullptr;
}

std::vector<std::string> StoryDirector::getActiveArcIds() const {
    std::vector<std::string> ids;
    for (auto& arc : m_arcs)
        if (arc.isActive && !arc.isCompleted) ids.push_back(arc.id);
    return ids;
}

// ============================================================================
// Update
// ============================================================================

void StoryDirector::update(float dt, WorldState& worldState) {
    m_timeSinceLastBeat += dt;

    for (auto& arc : m_arcs) {
        if (!arc.isActive || arc.isCompleted) continue;

        switch (arc.constraintMode) {
            case ArcConstraintMode::Scripted:
                evaluateScriptedArc(arc, worldState);
                break;
            case ArcConstraintMode::Guided:
                evaluateGuidedArc(arc, worldState);
                break;
            case ArcConstraintMode::Emergent:
                evaluateEmergentArc(arc, worldState);
                break;
            case ArcConstraintMode::Freeform:
                evaluateFreeformArc(arc, worldState);
                break;
        }

        arc.recalculateProgress();
    }

    updatePacing(dt, worldState);
}

// ============================================================================
// Scripted Mode — force beats in sequential order
// ============================================================================

void StoryDirector::evaluateScriptedArc(StoryArc& arc, WorldState& worldState) {
    for (auto& beat : arc.beats) {
        if (beat.status == BeatStatus::Completed || beat.status == BeatStatus::Skipped)
            continue;

        // Check prerequisites
        if (!arePrerequisitesMet(beat, arc)) continue;

        if (beat.status == BeatStatus::Pending) {
            // In scripted mode, check trigger condition OR force if prerequisites are met
            bool triggered = beat.triggerCondition.empty() ||
                             evaluateCondition(beat.triggerCondition, worldState);

            if (triggered) {
                beat.status = BeatStatus::Active;
                beat.activatedTime = worldState.worldTime;

                // Execute director actions for this beat
                for (auto& action : beat.directorActions) {
                    DirectorAction da;
                    da.type = action;
                    da.params = {{"beatId", beat.id}, {"arcId", arc.id}};
                    emitAction(std::move(da));
                }
            } else {
                // In scripted mode, stop at the first non-triggered beat
                break;
            }
        }

        if (beat.status == BeatStatus::Active) {
            // Check failure condition first
            if (!beat.failureCondition.empty() &&
                evaluateCondition(beat.failureCondition, worldState)) {
                beat.status = BeatStatus::Failed;
                beat.completedTime = worldState.worldTime;
                continue;
            }

            // Check completion
            if (!beat.completionCondition.empty() &&
                evaluateCondition(beat.completionCondition, worldState)) {
                beat.status = BeatStatus::Completed;
                beat.completedTime = worldState.worldTime;
                arc.lastBeatTime = worldState.worldTime;
                m_timeSinceLastBeat = 0.0f;
            } else {
                // In scripted mode, don't advance past the active beat
                break;
            }
        }
    }
}

// ============================================================================
// Guided Mode — flexible ordering, create opportunities
// ============================================================================

void StoryDirector::evaluateGuidedArc(StoryArc& arc, WorldState& worldState) {
    for (auto& beat : arc.beats) {
        if (beat.status == BeatStatus::Completed || beat.status == BeatStatus::Skipped)
            continue;

        if (!arePrerequisitesMet(beat, arc)) continue;

        // Pacing: respect minimum time between beats (recomputed per beat)
        float timeSinceArcBeat = worldState.worldTime - arc.lastBeatTime;
        bool canAdvance = (arc.lastBeatTime == 0.0f) || (timeSinceArcBeat >= arc.minTimeBetweenBeats);

        if (beat.status == BeatStatus::Pending && canAdvance) {
            bool triggered = beat.triggerCondition.empty() ||
                             evaluateCondition(beat.triggerCondition, worldState);

            if (triggered) {
                beat.status = BeatStatus::Active;
                beat.activatedTime = worldState.worldTime;

                for (auto& action : beat.directorActions) {
                    DirectorAction da;
                    da.type = action;
                    da.params = {{"beatId", beat.id}, {"arcId", arc.id}};
                    emitAction(std::move(da));
                }
            }
            // In guided mode, skip soft/optional beats that aren't triggering,
            // but block on hard beats
            else if (beat.type == BeatType::Hard) {
                // Check if stalled — if we've waited too long, emit a nudge action
                if (timeSinceArcBeat > arc.maxTimeWithoutProgress) {
                    DirectorAction nudge;
                    nudge.type = "nudge_beat";
                    nudge.params = {{"beatId", beat.id}, {"arcId", arc.id},
                                    {"reason", "stalled"}};
                    emitAction(std::move(nudge));
                }
            }
        }

        if (beat.status == BeatStatus::Active) {
            if (!beat.failureCondition.empty() &&
                evaluateCondition(beat.failureCondition, worldState)) {
                beat.status = BeatStatus::Failed;
                beat.completedTime = worldState.worldTime;
                continue;
            }

            if (!beat.completionCondition.empty() &&
                evaluateCondition(beat.completionCondition, worldState)) {
                beat.status = BeatStatus::Completed;
                beat.completedTime = worldState.worldTime;
                arc.lastBeatTime = worldState.worldTime;
                m_timeSinceLastBeat = 0.0f;
            }
        }
    }
}

// ============================================================================
// Emergent Mode — observe, inject catalysts
// ============================================================================

void StoryDirector::evaluateEmergentArc(StoryArc& arc, WorldState& worldState) {
    for (auto& beat : arc.beats) {
        if (beat.status == BeatStatus::Completed || beat.status == BeatStatus::Skipped)
            continue;

        // Hard beats are not allowed in emergent mode — skip them
        if (beat.type == BeatType::Hard) {
            beat.status = BeatStatus::Skipped;
            continue;
        }

        if (!arePrerequisitesMet(beat, arc)) continue;

        if (beat.status == BeatStatus::Pending) {
            // Only activate if trigger conditions are naturally met
            if (!beat.triggerCondition.empty() &&
                evaluateCondition(beat.triggerCondition, worldState)) {
                beat.status = BeatStatus::Active;
                beat.activatedTime = worldState.worldTime;
                // In emergent mode, director actions are suggestions, not commands
                for (auto& action : beat.directorActions) {
                    DirectorAction da;
                    da.type = action;
                    da.params = {{"beatId", beat.id}, {"arcId", arc.id},
                                 {"mode", "suggestion"}};
                    emitAction(std::move(da));
                }
            }
        }

        if (beat.status == BeatStatus::Active) {
            if (!beat.completionCondition.empty() &&
                evaluateCondition(beat.completionCondition, worldState)) {
                beat.status = BeatStatus::Completed;
                beat.completedTime = worldState.worldTime;
                arc.lastBeatTime = worldState.worldTime;
                m_timeSinceLastBeat = 0.0f;
            }
        }
    }
}

// ============================================================================
// Freeform Mode — no beats, pacing management only
// ============================================================================

void StoryDirector::evaluateFreeformArc(StoryArc& arc, WorldState& worldState) {
    // In freeform, all beats are skipped — pacing is handled by updatePacing()
    for (auto& beat : arc.beats) {
        if (beat.status == BeatStatus::Pending) {
            beat.status = BeatStatus::Skipped;
        }
    }
    arc.recalculateProgress();
}

// ============================================================================
// Condition Checking
// ============================================================================

bool StoryDirector::evaluateCondition(const std::string& condition, const WorldState& worldState) const {
    if (condition.empty()) return true;

    // Use custom evaluator if provided
    if (m_conditionEvaluator) {
        return m_conditionEvaluator(condition, worldState);
    }

    // Default: check if a world variable with this key exists and is truthy
    auto* var = worldState.getVariable(condition);
    if (!var) return false;

    return std::visit([](auto&& val) -> bool {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, bool>) return val;
        else if constexpr (std::is_same_v<T, int>) return val != 0;
        else if constexpr (std::is_same_v<T, float>) return val != 0.0f;
        else if constexpr (std::is_same_v<T, std::string>) return !val.empty();
        else return false;
    }, var->value);
}

bool StoryDirector::arePrerequisitesMet(const StoryBeat& beat, const StoryArc& arc) const {
    for (auto& prereqId : beat.prerequisites) {
        auto* prereq = arc.getBeat(prereqId);
        if (!prereq || prereq->status != BeatStatus::Completed)
            return false;
    }
    return true;
}

// ============================================================================
// Pacing
// ============================================================================

void StoryDirector::updatePacing(float dt, WorldState& worldState) {
    // Calculate recommended tension from all active arcs
    float targetTension = 0.0f;
    int activeCount = 0;

    for (auto& arc : m_arcs) {
        if (!arc.isActive || arc.isCompleted) continue;
        ++activeCount;

        if (!arc.tensionCurve.empty()) {
            targetTension += interpolateTensionCurve(arc.tensionCurve, arc.progress);
        }
    }

    if (activeCount > 0) {
        targetTension /= static_cast<float>(activeCount);

        // Smoothly move world tension toward target
        float diff = targetTension - worldState.dramaTension;
        float rate = 0.1f; // Tension change rate per second
        worldState.dramaTension += diff * std::min(1.0f, rate * dt);
        worldState.dramaTension = std::clamp(worldState.dramaTension, 0.0f, 1.0f);
    }
}

float StoryDirector::interpolateTensionCurve(const std::vector<float>& curve, float progress) const {
    if (curve.empty()) return 0.0f;
    if (curve.size() == 1) return curve[0];

    // Map progress (0-1) to curve index
    float fIndex = progress * static_cast<float>(curve.size() - 1);
    int lo = static_cast<int>(fIndex);
    int hi = std::min(lo + 1, static_cast<int>(curve.size()) - 1);
    float frac = fIndex - static_cast<float>(lo);

    return curve[lo] * (1.0f - frac) + curve[hi] * frac;
}

// ============================================================================
// Actions
// ============================================================================

void StoryDirector::emitAction(DirectorAction action) {
    m_pendingActions.push_back(action);
    if (m_actionCallback) {
        m_actionCallback(action);
    }
}

void StoryDirector::listenTo(EventBus& bus) {
    if (m_eventBusSubscriptionId >= 0) {
        bus.unsubscribe(m_eventBusSubscriptionId);
    }
    m_eventBusSubscriptionId = bus.subscribe(
        [this](const WorldEvent& event) { onWorldEvent(event); });
}

void StoryDirector::onWorldEvent(const WorldEvent& event) {
    // Check if this event completes or triggers any active beats
    // The actual evaluation happens in update() via condition checks
    // This could be used for immediate reactions in the future
}

// === Convenience action emitters ===

void StoryDirector::injectEvent(const WorldEvent& event) {
    DirectorAction action;
    action.type = "inject_event";
    action.params = {{"eventId", event.id}, {"eventType", event.type},
                     {"importance", event.importance}};
    emitAction(std::move(action));
}

void StoryDirector::promoteCharacterAgency(const std::string& characterId, AgencyLevel level) {
    DirectorAction action;
    action.type = "promote_agency";
    action.params = {{"characterId", characterId},
                     {"level", agencyLevelToString(level)}};
    emitAction(std::move(action));
}

void StoryDirector::suggestGoal(const std::string& characterId, const std::string& goalId,
                                const std::string& description, float priority) {
    DirectorAction action;
    action.type = "suggest_goal";
    action.params = {{"characterId", characterId}, {"goalId", goalId},
                     {"description", description}, {"priority", priority}};
    emitAction(std::move(action));
}

void StoryDirector::modifyFactionRelation(const std::string& factionA,
                                           const std::string& factionB, float delta) {
    DirectorAction action;
    action.type = "modify_faction";
    action.params = {{"factionA", factionA}, {"factionB", factionB}, {"delta", delta}};
    emitAction(std::move(action));
}

void StoryDirector::setWorldVariable(const std::string& key, const std::string& value) {
    DirectorAction action;
    action.type = "set_variable";
    action.params = {{"key", key}, {"value", value}};
    emitAction(std::move(action));
}

void StoryDirector::skipBeat(const std::string& arcId, const std::string& beatId) {
    auto* arc = getArcMut(arcId);
    if (!arc) return;
    auto* beat = arc->getBeatMut(beatId);
    if (!beat) return;
    beat->status = BeatStatus::Skipped;
    arc->recalculateProgress();
}

void StoryDirector::completeBeat(const std::string& arcId, const std::string& beatId, float worldTime) {
    auto* arc = getArcMut(arcId);
    if (!arc) return;
    auto* beat = arc->getBeatMut(beatId);
    if (!beat) return;
    beat->status = BeatStatus::Completed;
    beat->completedTime = worldTime;
    arc->lastBeatTime = worldTime;
    arc->recalculateProgress();
    m_timeSinceLastBeat = 0.0f;
}

// ============================================================================
// Queries
// ============================================================================

float StoryDirector::getTargetTension(const std::string& arcId) const {
    auto* arc = getArc(arcId);
    if (!arc || arc->tensionCurve.empty()) return 0.0f;
    return interpolateTensionCurve(arc->tensionCurve, arc->progress);
}

float StoryDirector::getRecommendedTension() const {
    float total = 0.0f;
    int count = 0;
    for (auto& arc : m_arcs) {
        if (!arc.isActive || arc.isCompleted || arc.tensionCurve.empty()) continue;
        total += interpolateTensionCurve(arc.tensionCurve, arc.progress);
        ++count;
    }
    return count > 0 ? total / static_cast<float>(count) : 0.0f;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json StoryDirector::saveState() const {
    nlohmann::json j;
    j["arcs"] = nlohmann::json::array();
    for (auto& arc : m_arcs)
        j["arcs"].push_back(arc);
    j["timeSinceLastBeat"] = m_timeSinceLastBeat;
    return j;
}

void StoryDirector::loadState(const nlohmann::json& state) {
    m_arcs.clear();
    if (state.contains("arcs")) {
        for (auto& aj : state["arcs"])
            m_arcs.push_back(aj.get<StoryArc>());
    }
    if (state.contains("timeSinceLastBeat"))
        state.at("timeSinceLastBeat").get_to(m_timeSinceLastBeat);
}

} // namespace Story
} // namespace Phyxel
