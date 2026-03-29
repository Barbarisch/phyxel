#include "scene/behaviors/ScheduledBehavior.h"
#include "scene/Entity.h"
#include "core/LocationRegistry.h"
#include "graphics/DayNightCycle.h"

namespace Phyxel {
namespace Scene {

ScheduledBehavior::ScheduledBehavior(AI::Schedule schedule)
    : m_schedule(std::move(schedule)) {}

ScheduledBehavior::ScheduledBehavior(AI::Schedule schedule, std::shared_ptr<AI::UtilityBrain> brain)
    : BehaviorTreeBehavior(std::move(brain)), m_schedule(std::move(schedule)) {}

ScheduledBehavior::ScheduledBehavior(AI::Schedule schedule, AI::BTNodePtr rootTree)
    : BehaviorTreeBehavior(std::move(rootTree)), m_schedule(std::move(schedule)) {}

void ScheduledBehavior::update(float dt, NPCContext& ctx) {
    // Periodically re-evaluate schedule (not every frame)
    m_scheduleCheckTimer -= dt;
    if (m_scheduleCheckTimer <= 0.0f) {
        m_scheduleCheckTimer = m_scheduleCheckInterval;
        updateScheduleState(ctx);
    }

    // Let the base class handle perception, blackboard, and tree/brain ticking
    BehaviorTreeBehavior::update(dt, ctx);
}

void ScheduledBehavior::updateScheduleState(NPCContext& ctx) {
    auto& bb = getBlackboard();

    // Read current hour from DayNightCycle
    float currentHour = 12.0f;  // Default to noon if no cycle
    bool isNight = false;
    int dayNumber = 0;

    if (ctx.dayNightCycle) {
        currentHour = ctx.dayNightCycle->getTimeOfDay();
        isNight = ctx.dayNightCycle->isNight();
        dayNumber = ctx.dayNightCycle->getDayNumber();
    }

    bb.set("currentHour", currentHour);
    bb.set("isNight", isNight);
    bb.set("dayNumber", dayNumber);

    // Look up schedule entry for the current hour
    const auto* entry = m_schedule.getCurrentActivity(currentHour);
    if (entry) {
        m_currentActivityName = AI::ScheduleEntry::activityToString(entry->activity);
        m_currentLocationId = entry->locationId;

        bb.set("currentActivity", m_currentActivityName);
        bb.set("taskLocationId", entry->locationId);

        // Look up location position from registry
        if (ctx.locationRegistry && !entry->locationId.empty()) {
            const auto* loc = ctx.locationRegistry->getLocation(entry->locationId);
            if (loc) {
                bb.set("taskLocationPos", loc->position);

                // Check if NPC is at the target location
                if (ctx.self) {
                    float dist = glm::length(ctx.self->getPosition() - loc->position);
                    bb.set("atTaskLocation", dist <= loc->radius);
                }
            }
        }
    } else {
        // No schedule entry for this hour — default to wander
        m_currentActivityName = "Wander";
        m_currentLocationId = "";
        bb.set("currentActivity", std::string("Wander"));
        bb.set("taskLocationId", std::string(""));
    }
}

} // namespace Scene
} // namespace Phyxel
