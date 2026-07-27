#include "scene/behaviors/ScheduledBehavior.h"
#include "scene/Entity.h"
#include "core/LocationRegistry.h"
#include "core/NavGraph.h"
#include "core/PathService.h"
#include "graphics/DayNightCycle.h"

#include <cmath>

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

    // A wired brain/tree owns movement; otherwise the built-in mover walks the NPC
    // to its scheduled location (a plain `scheduled` NPC used to stand still forever).
    if (!getUtilityBrain() && !getTree())
        updateMovement(dt, ctx);
}

void ScheduledBehavior::updateMovement(float dt, NPCContext& ctx) {
    if (!ctx.self) return;
    auto& bb = getBlackboard();   // moverState: diagnosable via the blackboard API
    if (m_replanCooldown > 0.0f) m_replanCooldown -= dt;

    // Resolve the schedule's destination; no target (or Wander) = hold position.
    const Core::Location* loc = nullptr;
    if (!m_currentLocationId.empty() && ctx.locationRegistry)
        loc = ctx.locationRegistry->getLocation(m_currentLocationId);
    if (!loc) {
        bb.set("moverState", std::string("no_location"));
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return;
    }

    const glm::vec3 pos = ctx.self->getPosition();
    const glm::vec3 dest = loc->position;
    // Crowd separation (published by NPCManager): blended into every steering state
    // so a pack in a pinch point spreads instead of grinding into one wall cell.
    const glm::vec3 sep = bb.getVec3("sepPush", glm::vec3(0.0f));
    const float distXZ = glm::length(glm::vec2(dest.x - pos.x, dest.z - pos.z));
    if (distXZ <= loc->radius) {                     // arrived — perform the activity here
        m_path.clear();
        m_hasPathTarget = false;
        bb.set("moverState", std::string("arrived"));
        // Gentle drift apart while idling at the spot (a packed tavern door mills
        // out into a loose crowd instead of a column of interpenetrating bodies).
        ctx.self->setMoveVelocity(sep * (m_walkSpeed * 0.4f));
        return;
    }

    // Route via NavGraph/PathService when available (async preferred); direct-steer
    // fallback otherwise. Mirrors StoryDrivenBehavior::updateMovement.
    glm::vec3 steerTo = dest;
    if (ctx.navGraph || ctx.pathService) {
        const bool destChanged = !m_hasPathTarget || glm::distance(m_pathTarget, dest) > 0.5f;
        if (destChanged || (m_path.empty() && !m_pathPending && m_replanCooldown <= 0.0f))
            replanPath(ctx, pos, dest);

        if (m_pathPending && ctx.pathService) {
            Core::NavGraph::PathResult res;
            if (ctx.pathService->tryGetResult(m_pathHandle, res)) {
                m_pathPending = false;
                m_pathHandle = 0;
                if (res.found) { m_path = std::move(res.waypoints); m_pathIndex = 0; }
                else m_replanCooldown = 3.0f;   // failed route: back off before retrying
            }
        }
        if (m_pathPending) {                         // query in flight — hold, poll next frame
            bb.set("moverState", std::string("path_pending"));
            ctx.self->setMoveVelocity(glm::vec3(0.0f));
            return;
        }
        if (m_path.empty()) {
            // No route. A crowd can shove an NPC onto a cell the NavGraph cannot
            // resolve as a START (measured: 3 residents pinned at the tavern door
            // corner all night) — holding forever would strand them. Escape-steer
            // straight toward the destination during the backoff window; the next
            // replan runs from wherever that got us.
            bb.set("moverState", std::string("no_route"));
            glm::vec3 dir0 = glm::normalize(glm::vec3(dest.x - pos.x, 0.0f,
                                                      dest.z - pos.z));
            // Same sidestep bias as the walking branch: a straight escape can pin
            // against the very fence corner that made the start unresolvable —
            // alternate ~±75° per stuck strike to slide out of the pocket.
            if (m_stuckStrikes > 0) {
                const float a = (m_stuckStrikes % 2 == 1) ? 1.3f : -1.3f;
                const float c = std::cos(a), s = std::sin(a);
                dir0 = glm::normalize(glm::vec3(dir0.x * c - dir0.z * s, 0.0f,
                                                dir0.x * s + dir0.z * c));
            }
            glm::vec3 v0 = dir0 * m_walkSpeed + sep * (m_walkSpeed * 0.8f);
            if (glm::length(v0) > m_walkSpeed) v0 = glm::normalize(v0) * m_walkSpeed;
            ctx.self->setMoveVelocity(v0);
            ctx.self->setRotation(glm::angleAxis(std::atan2(dir0.x, dir0.z),
                                                 glm::vec3(0, 1, 0)));
            // The stuck watchdog must also run while escape-steering (it lives at the
            // end of the walking branch, which this path returns before reaching).
            m_progressTimer += dt;
            if (m_progressTimer >= 2.5f) {
                const float moved = glm::length(glm::vec2(pos.x - m_progressAnchor.x,
                                                          pos.z - m_progressAnchor.z));
                if (moved < 0.5f) ++m_stuckStrikes;
                else m_stuckStrikes = 0;
                m_progressAnchor = pos;
                m_progressTimer = 0.0f;
            }
            return;
        }
        while (m_pathIndex < m_path.size()) {
            glm::vec3 d = m_path[m_pathIndex] - pos;
            if (glm::length(glm::vec2(d.x, d.z)) < 0.5f) ++m_pathIndex;
            else break;
        }
        if (m_pathIndex >= m_path.size()) {          // path exhausted; replan next frame
            m_path.clear();
            m_hasPathTarget = false;
            bb.set("moverState", std::string("path_exhausted"));
            ctx.self->setMoveVelocity(glm::vec3(0.0f));
            return;
        }
        steerTo = m_path[m_pathIndex];
    }

    glm::vec3 diff = steerTo - pos;
    const float stepXZ = glm::length(glm::vec2(diff.x, diff.z));
    if (stepXZ < 0.4f) { ctx.self->setMoveVelocity(glm::vec3(0.0f)); return; }
    glm::vec3 dir = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
    // Repeatedly stuck on the same spot: the blocker is likely nav-INVISIBLE (placed
    // furniture — e.g. the street well — isn't chunk voxels, so replans return the
    // same straight line through it). Bias the heading ~75 degrees, alternating
    // sides per strike, to walk around the obstacle.
    if (m_stuckStrikes > 0) {
        const float a = (m_stuckStrikes % 2 == 1) ? 1.3f : -1.3f;   // ~±75°
        const float c = std::cos(a), s = std::sin(a);
        dir = glm::normalize(glm::vec3(dir.x * c - dir.z * s, 0.0f,
                                       dir.x * s + dir.z * c));
    }
    bb.set("moverState", std::string("walking"));
    glm::vec3 vel = dir * m_walkSpeed + sep * (m_walkSpeed * 0.8f);
    if (glm::length(vel) > m_walkSpeed) vel = glm::normalize(vel) * m_walkSpeed;
    ctx.self->setMoveVelocity(vel);                  // gravity handles Y
    ctx.self->setRotation(glm::angleAxis(std::atan2(dir.x, dir.z), glm::vec3(0, 1, 0)));

    // Stuck watchdog: commanding walk but making no ground (shoved off-path into a
    // fence, smoothed segment now blocked). <0.5 units of XZ progress over 2.5 s
    // -> drop the route and replan immediately from wherever we actually are.
    m_progressTimer += dt;
    if (m_progressTimer >= 2.5f) {
        const float moved = glm::length(glm::vec2(pos.x - m_progressAnchor.x,
                                                  pos.z - m_progressAnchor.z));
        if (moved < 0.5f) {
            invalidatePath();
            m_replanCooldown = 0.0f;
            ++m_stuckStrikes;
            bb.set("moverState", std::string("stuck_replan"));
        } else {
            m_stuckStrikes = 0;
        }
        m_progressAnchor = pos;
        m_progressTimer = 0.0f;
    }
}

void ScheduledBehavior::replanPath(NPCContext& ctx, const glm::vec3& from, const glm::vec3& to) {
    m_path.clear();
    m_pathIndex = 0;
    m_pathTarget = to;
    m_hasPathTarget = true;

    Core::NavAgentProfile agent;   // default humanoid

    if (ctx.pathService) {
        if (m_pathPending && m_pathHandle) ctx.pathService->cancel(m_pathHandle);
        m_pathHandle = ctx.pathService->requestPath(agent, from, to);
        m_pathPending = (m_pathHandle != 0);
        return;
    }

    m_pathPending = false;
    if (!ctx.navGraph) return;
    auto result = ctx.navGraph->findPath(from, to, agent);
    if (result.found) {
        m_path = result.waypoints.size() > 2
                     ? ctx.navGraph->smoothWaypoints(result.waypoints, agent)
                     : std::move(result.waypoints);
    } else {
        m_replanCooldown = 3.0f;   // failed sync route: back off (no per-frame A* spam)
    }
}

void ScheduledBehavior::invalidatePath() {
    // Keep m_pathPending/m_pathHandle: replanPath cancels a superseded in-flight query.
    m_path.clear();
    m_pathIndex = 0;
    m_hasPathTarget = false;
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
