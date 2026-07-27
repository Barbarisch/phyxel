#pragma once

#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "ai/Schedule.h"
#include <string>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

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

    /// Current path waypoints (for nav invalidation on world edits — see
    /// NPCManager::onRegionChanged).
    const std::vector<glm::vec3>& getPathWaypoints() const { return m_path; }

    /// Drop the current route so the next update replans (world changed under it).
    void invalidatePath();

    void setWalkSpeed(float s) { m_walkSpeed = s; }

private:
    /// Write schedule-derived state into the blackboard.
    void updateScheduleState(NPCContext& ctx);

    /// Built-in mover: walk to the scheduled location via NavGraph/PathService.
    /// Runs only when NO brain/tree is wired (a wired brain owns movement) — without
    /// this a plain `scheduled` NPC resolved its target and then stood still forever.
    void updateMovement(float dt, NPCContext& ctx);
    void replanPath(NPCContext& ctx, const glm::vec3& from, const glm::vec3& to);

    AI::Schedule m_schedule;
    float m_scheduleCheckInterval = 1.0f;    ///< How often to re-evaluate schedule (seconds)
    float m_scheduleCheckTimer = 0.0f;
    std::string m_currentActivityName;
    std::string m_currentLocationId;

    // Built-in mover state (mirrors StoryDrivenBehavior's; extraction into a shared
    // NavMover is a queued cleanup — story's copy is live-verified, left untouched).
    std::vector<glm::vec3> m_path;
    size_t    m_pathIndex = 0;
    bool      m_pathPending = false;
    uint64_t  m_pathHandle = 0;
    glm::vec3 m_pathTarget{0.0f};
    bool      m_hasPathTarget = false;
    float     m_walkSpeed = 2.0f;
    float     m_replanCooldown = 0.0f;   ///< backoff after a failed route (no goal spam)
    // Stuck detection: crowds shove NPCs off their path (or onto cells the NavGraph
    // can't resolve as a start) — measured: door-crowd members treadmilling against a
    // fence at constant distance. Windowed progress check -> drop the path + replan.
    glm::vec3 m_progressAnchor{0.0f};
    float     m_progressTimer = 0.0f;
    int       m_stuckStrikes = 0;   ///< consecutive stuck windows -> sidestep bias
                                    ///< (placed furniture is nav-invisible; a well on
                                    ///< the street walls a straight path — measured)
};

} // namespace Scene
} // namespace Phyxel
