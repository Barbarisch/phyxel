#pragma once

#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "ai/Schedule.h"
#include <string>
#include <memory>

namespace Phyxel {

// Forward declarations
namespace Core { class LocationRegistry; }
namespace Graphics { class DayNightCycle; }

namespace Scene {

/// Extends BehaviorTreeBehavior with time-aware scheduling.
/// Each frame, queries the DayNightCycle for current hour, looks up the
/// active ScheduleEntry, writes schedule state to the Blackboard, and
/// then ticks the underlying behavior tree/utility brain.
///
/// Blackboard keys written:
///   "currentActivity" (string)  — e.g. "Work", "Sleep", "Patrol"
///   "taskLocationId" (string)   — location ID from schedule entry
///   "taskLocationPos" (vec3)    — world position of the target location
///   "atTaskLocation" (bool)     — whether NPC is within arrival radius
///   "currentHour" (float)       — current world hour (0-24)
///   "isNight" (bool)            — from DayNightCycle
///   "dayNumber" (int)           — from DayNightCycle
class ScheduledBehavior : public BehaviorTreeBehavior {
public:
    /// Construct with a schedule. Brain/tree can be set separately or via base constructors.
    explicit ScheduledBehavior(AI::Schedule schedule);

    /// Construct with a schedule and a UtilityBrain.
    ScheduledBehavior(AI::Schedule schedule, std::shared_ptr<AI::UtilityBrain> brain);

    /// Construct with a schedule and a plain BehaviorTree.
    ScheduledBehavior(AI::Schedule schedule, AI::BTNodePtr rootTree);

    void update(float dt, NPCContext& ctx) override;
    std::string getBehaviorName() const override { return "Scheduled"; }

    // Schedule access
    const AI::Schedule& getSchedule() const { return m_schedule; }
    AI::Schedule& getSchedule() { return m_schedule; }
    void setSchedule(AI::Schedule schedule) { m_schedule = std::move(schedule); }

    /// Get the current activity name (from last update).
    const std::string& getCurrentActivityName() const { return m_currentActivityName; }

    /// Get the current target location ID (from last update).
    const std::string& getCurrentLocationId() const { return m_currentLocationId; }

private:
    /// Write schedule-derived state into the blackboard.
    void updateScheduleState(NPCContext& ctx);

    AI::Schedule m_schedule;
    float m_scheduleCheckInterval = 1.0f;    ///< How often to re-evaluate schedule (seconds)
    float m_scheduleCheckTimer = 0.0f;
    std::string m_currentActivityName;
    std::string m_currentLocationId;
};

} // namespace Scene
} // namespace Phyxel
