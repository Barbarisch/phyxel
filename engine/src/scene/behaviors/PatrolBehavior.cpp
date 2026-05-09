#include "scene/behaviors/PatrolBehavior.h"
#include "scene/Entity.h"
#include "core/EntityRegistry.h"
#include "core/ChunkManager.h"
#include "core/AStarPathfinder.h"
#include "core/NavGrid.h"
#include "graphics/RaycastVisualizer.h"
#include "ui/SpeechBubbleManager.h"
#include "utils/Logger.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/compatibility.hpp>
#include <random>
#include <cmath>

namespace Phyxel {
namespace Scene {

PatrolBehavior::PatrolBehavior(const std::vector<glm::vec3>& waypoints, float walkSpeed,
                               float waitTime)
    : m_waypoints(waypoints), m_walkSpeed(walkSpeed), m_waitTime(waitTime) {}

void PatrolBehavior::update(float dt, NPCContext& ctx) {
    if (m_waypoints.empty() || !ctx.self) return;

    // Derive forward from entity rotation (model faces +Z; atan2(x,z) yaw convention)
    glm::vec3 forward = ctx.self->getRotation() * glm::vec3(0.0f, 0.0f, 1.0f);

    // Update perception (FOV cone, LOS) every frame
    updatePerception(dt, ctx, forward);

    // Phase 1 — Forward terrain probe: every PROBE_INTERVAL frames check upcoming
    // path nodes against the live NavGrid. Invalidates stale paths immediately
    // instead of waiting up to 1.5 s for the stuck timer.
    if (m_pathComputed && !m_pathNodes.empty() && m_pathfinder) {
        if (++m_probeFrameCounter >= PROBE_INTERVAL) {
            m_probeFrameCounter = 0;
            validateAheadOfPath();
        }
    }

    if (m_waiting) {
        m_waitTimer += dt;
        // Stop movement while waiting
        ctx.self->setMoveVelocity(glm::vec3(0.0f));

        // Look-around sweep while waiting at waypoint
        m_lookAroundPhase += dt;
        float sweep = std::sin(m_lookAroundPhase * 2.0f) * 40.0f; // +/- 40 degrees
        float yawRad = glm::radians(m_baseYaw + sweep);
        ctx.self->setRotation(glm::angleAxis(yawRad, glm::vec3(0, 1, 0)));

        if (m_waitTimer >= m_waitTime) {
            m_waiting = false;
            m_waitTimer = 0.0f;
            m_currentWaypoint = (m_currentWaypoint + 1) % m_waypoints.size();
            m_pathComputed = false; // Need new path for next waypoint
        }
        return;
    }

    // Path retry cooldown
    if (m_pathRetryTimer > 0.0f) {
        m_pathRetryTimer -= dt;
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return;
    }

    // Compute path to current waypoint if needed
    glm::vec3 pos = ctx.self->getPosition();
    glm::vec3 target = m_waypoints[m_currentWaypoint];

    if (!m_pathComputed) {
        computePath(pos, target);
    }

    // Determine immediate movement target
    glm::vec3 moveTarget;
    float arrivalThreshold;

    if (!m_pathNodes.empty() && m_currentPathNode < m_pathNodes.size()) {
        // Following computed path
        moveTarget = m_pathNodes[m_currentPathNode];
        arrivalThreshold = PATH_NODE_THRESHOLD;
    } else {
        // Direct-line fallback (no pathfinder or path failed)
        moveTarget = target;
        arrivalThreshold = ARRIVAL_THRESHOLD;
    }

    glm::vec3 diff = moveTarget - pos;
    float distXZ = glm::length(glm::vec2(diff.x, diff.z));

    if (distXZ < arrivalThreshold) {
        if (!m_pathNodes.empty() && m_currentPathNode < m_pathNodes.size()) {
            // Advance to next path node
            ++m_currentPathNode;
            m_linkJumpTriggered = false; // reset for the next node
            if (m_currentPathNode >= m_pathNodes.size()) {
                // Reached end of path = arrived at waypoint
                goto arrived;
            }
            return; // Continue to next node next frame
        }

arrived:
        // Arrived at waypoint — stop moving
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        m_waiting = true;
        m_waitTimer = 0.0f;
        m_lookAroundPhase = 0.0f;
        m_pathComputed = false;
        m_pathNodes.clear();
        m_pathNodeTypes.clear();
        m_currentPathNode = 0;
        m_linkJumpTriggered = false;
        m_consecutiveFailedPaths = 0; // Reached destination — reset failure count

        // Capture base yaw for look-around sweep (face toward next waypoint)
        size_t nextWP = (m_currentWaypoint + 1) % m_waypoints.size();
        glm::vec3 toNext = m_waypoints[nextWP] - pos;
        if (glm::length(glm::vec2(toNext.x, toNext.z)) > 0.01f) {
            m_baseYaw = glm::degrees(std::atan2(toNext.x, toNext.z));
        }

        // Ambient speech bubble
        if (!m_arrivalPhrases.empty() && ctx.speechBubbleManager && !ctx.selfId.empty()) {
            static thread_local std::mt19937 rng(std::random_device{}());
            std::uniform_real_distribution<float> roll(0.0f, 1.0f);
            if (roll(rng) < m_speechChance) {
                std::uniform_int_distribution<size_t> pick(0, m_arrivalPhrases.size() - 1);
                ctx.speechBubbleManager->say(ctx.selfId, m_arrivalPhrases[pick(rng)], 3.0f);
            }
        }
        return;
    }

    // Phase 3 — Jump link execution: trigger jump() once when targeting a LinkJump waypoint.
    if (!m_pathNodeTypes.empty() && m_currentPathNode < m_pathNodeTypes.size() &&
        m_pathNodeTypes[m_currentPathNode] == Core::WaypointType::LinkJump &&
        !m_linkJumpTriggered) {
        ctx.self->jump();
        m_linkJumpTriggered = true;
        LOG_DEBUG("PatrolBehavior", "Jump link triggered toward ({},{},{})",
                  moveTarget.x, moveTarget.y, moveTarget.z);
    }

    // Phase 5 — Arrival deceleration: slow down when close to the next waypoint.
    float effectiveSpeed = m_walkSpeed;
    if (distXZ < DECEL_RADIUS) {
        effectiveSpeed *= glm::clamp(distXZ / DECEL_RADIUS, 0.3f, 1.0f);
    }

    // Compute XZ direction toward current movement target and set velocity (gravity handles Y).
    // The NPC's capsule collider naturally slides over subcube-height steps (~0.333 blocks).
    glm::vec3 direction = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));

    // Phase 5 — NPC separation: push away from nearby NPCs to prevent piling up.
    if (ctx.entityRegistry) {
        auto nearby = ctx.entityRegistry->getEntitiesNear(pos, 1.5f);
        glm::vec3 separation(0.0f);
        for (const auto& [id, ent] : nearby) {
            if (ent == ctx.self) continue;
            glm::vec3 away = pos - ent->getPosition();
            float dist = glm::length(away);
            if (dist > 0.01f && dist < 1.5f) {
                separation += (away / dist) * (1.0f - dist / 1.5f);
            }
        }
        // Add up to 30% of walk speed as separation offset
        float sepLen = glm::length(glm::vec2(separation.x, separation.z));
        if (sepLen > 0.01f) {
            glm::vec3 sepDir(separation.x / sepLen, 0.0f, separation.z / sepLen);
            direction = glm::normalize(direction + sepDir * 0.3f);
        }
    }

    ctx.self->setMoveVelocity(direction * effectiveSpeed);

    // Stuck detection: if NPC hasn't moved significantly in 1.5s, invalidate path and recompute.
    // After 5s still stuck (e.g. NavGrid has not yet updated), teleport to nearest safe cell.
    float distFromLast = glm::length(pos - m_lastLoggedPos);
    if (distFromLast < 0.1f) {
        m_stuckTimer += dt;
        if (m_stuckTimer > 5.0f && m_pathfinder) {
            // Last-resort: teleport to nearest non-nearWall cell
            auto* grid = m_pathfinder->getGrid();
            if (grid) {
                const Core::NavCell* safe = grid->findNearestNonWall(pos);
                if (safe) {
                    glm::vec3 safePos(safe->x + 0.5f, safe->surfaceY + 1.0f, safe->z + 0.5f);
                    LOG_WARN("PatrolBehavior", "STUCK RECOVERY (teleport): ({},{},{}) -> ({},{},{})",
                             pos.x, pos.y, pos.z, safePos.x, safePos.y, safePos.z);
                    ctx.self->setPosition(safePos);
                }
            }
            invalidatePath();
            m_stuckTimer = 0.0f;
            m_lastLoggedPos = pos;
        } else if (m_stuckTimer > 1.5f) {
            // Dynamic replan: obstacle may have appeared; recompute path
            LOG_WARN("PatrolBehavior", "STUCK (replan): pos=({},{},{}), pathNode={}/{}, wp={}",
                     pos.x, pos.y, pos.z,
                     m_currentPathNode, m_pathNodes.size(), m_currentWaypoint);
            invalidatePath();
            m_stuckTimer = 0.0f;
            m_lastLoggedPos = pos;
        }
    } else {
        m_stuckTimer = 0.0f;
        m_lastLoggedPos = pos;
    }

    // Phase 5 — Smooth turning: lerp yaw toward movement direction instead of instant snap.
    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.01f) {
        float targetYaw = std::atan2(direction.x, direction.z);
        // Normalize angular difference to [-pi, pi]
        float diff_yaw = targetYaw - m_currentYaw;
        while (diff_yaw >  glm::pi<float>()) diff_yaw -= glm::two_pi<float>();
        while (diff_yaw < -glm::pi<float>()) diff_yaw += glm::two_pi<float>();
        m_currentYaw += diff_yaw * glm::clamp(TURN_SPEED * dt, 0.0f, 1.0f);
        ctx.self->setRotation(glm::angleAxis(m_currentYaw, glm::vec3(0, 1, 0)));
    }

    // Draw active path as cyan lines in F5 debug overlay
    if (ctx.raycastVisualizer && ctx.raycastVisualizer->isEnabled() && !m_pathNodes.empty()) {
        const glm::vec3 pathColor(0.0f, 0.9f, 0.9f);   // Cyan
        const glm::vec3 nodeColor(1.0f, 1.0f, 0.0f);   // Yellow node markers
        const float nodeHalf = 0.1f;
        glm::vec3 prev = pos + glm::vec3(0.0f, 0.5f, 0.0f);
        for (size_t i = m_currentPathNode; i < m_pathNodes.size(); ++i) {
            glm::vec3 node = m_pathNodes[i] + glm::vec3(0.0f, 0.5f, 0.0f);
            ctx.raycastVisualizer->addLine(prev, node, pathColor);
            // Small cross at each node
            ctx.raycastVisualizer->addLine(node - glm::vec3(nodeHalf, 0, 0), node + glm::vec3(nodeHalf, 0, 0), nodeColor);
            ctx.raycastVisualizer->addLine(node - glm::vec3(0, 0, nodeHalf), node + glm::vec3(0, 0, nodeHalf), nodeColor);
            prev = node;
        }
    }
}

void PatrolBehavior::onInteract(Entity* interactor) {
    LOG_INFO("PatrolBehavior", "NPC on patrol interacted with");
    // Pause patrol briefly on interaction (stop for one wait cycle)
    m_waiting = true;
    m_waitTimer = 0.0f;
}

void PatrolBehavior::onEvent(const std::string& eventType, const nlohmann::json& data) {
    // Could respond to "alarm" events by changing waypoints, etc.
}

void PatrolBehavior::setWaypoints(const std::vector<glm::vec3>& waypoints) {
    m_waypoints = waypoints;
    m_currentWaypoint = 0;
    m_waiting = false;
    m_waitTimer = 0.0f;
    m_pathComputed = false;
    m_pathNodes.clear();
    m_pathNodeTypes.clear();
    m_currentPathNode = 0;
    m_linkJumpTriggered = false;
}

void PatrolBehavior::computePath(const glm::vec3& from, const glm::vec3& to) {
    m_pathComputed = true;
    m_pathNodes.clear();
    m_currentPathNode = 0;

    if (!m_pathfinder) {
        LOG_WARN("PatrolBehavior", "No pathfinder set — direct-line fallback from ({},{},{}) to ({},{},{})",
                 from.x, from.y, from.z, to.x, to.y, to.z);
        return; // No pathfinder = direct-line fallback
    }

    auto result = m_pathfinder->findPath(from, to);
    if (result.found && !result.waypoints.empty()) {
        m_pathNodes = std::move(result.waypoints);
        m_pathNodeTypes = std::move(result.waypointTypes);
        // Ensure types vector is always same length as nodes
        m_pathNodeTypes.resize(m_pathNodes.size(), Core::WaypointType::Normal);
        m_consecutiveFailedPaths = 0; // Reset failure counter on success
        LOG_INFO("PatrolBehavior", "Path found: ({},{},{}) -> ({},{},{}), {} waypoints, {} nodes expanded",
                 from.x, from.y, from.z, to.x, to.y, to.z,
                 m_pathNodes.size(), result.nodesExpanded);
        for (size_t wi = 0; wi < m_pathNodes.size(); ++wi) {
            LOG_DEBUG("PatrolBehavior", "  wp[{}]: ({},{},{})", wi,
                      m_pathNodes[wi].x, m_pathNodes[wi].y, m_pathNodes[wi].z);
        }
    } else {
        ++m_consecutiveFailedPaths;
        m_pathComputed = false;

        if (m_consecutiveFailedPaths >= PATH_FAILURE_RETREAT && m_waypoints.size() > 1) {
            // Path is blocked — retreat to previous waypoint and wait before retrying
            size_t prevWP = (m_currentWaypoint + m_waypoints.size() - 1) % m_waypoints.size();
            LOG_WARN("PatrolBehavior",
                     "Path blocked {} times — retreating from wp {} to wp {}",
                     m_consecutiveFailedPaths, m_currentWaypoint, prevWP);
            m_currentWaypoint = prevWP;
            m_consecutiveFailedPaths = 0;
            m_pathRetryTimer = RETREAT_RETRY_DELAY; // Longer pause at retreat point
        } else {
            m_pathRetryTimer = PATH_RETRY_DELAY;
            LOG_WARN("PatrolBehavior", "Path NOT found ({}/{}): ({},{},{}) -> ({},{},{}), {} nodes expanded",
                     m_consecutiveFailedPaths, PATH_FAILURE_RETREAT,
                     from.x, from.y, from.z, to.x, to.y, to.z, result.nodesExpanded);
        }
    }
}

void PatrolBehavior::validateAheadOfPath() {
    if (!m_pathfinder) return;
    const Core::NavGrid* grid = m_pathfinder->getGrid();
    if (!grid) return;

    // Check the next PROBE_LOOKAHEAD nodes against the live NavGrid.
    // If any node's cell is now non-walkable or surfaceY has shifted significantly,
    // invalidate immediately so the path is replanned within PROBE_INTERVAL frames.
    size_t probeEnd = std::min(m_currentPathNode + static_cast<size_t>(PROBE_LOOKAHEAD),
                               m_pathNodes.size());
    for (size_t i = m_currentPathNode; i < probeEnd; ++i) {
        const glm::vec3& wp = m_pathNodes[i];
        int cx = static_cast<int>(std::floor(wp.x));
        int cz = static_cast<int>(std::floor(wp.z));
        const Core::NavCell* cell = grid->getCell(cx, cz);

        bool invalid = !cell ||
                       !cell->walkable ||
                       std::abs(cell->surfaceY - static_cast<int>(wp.y - 1.0f)) > 1;
        if (invalid) {
            LOG_DEBUG("PatrolBehavior",
                      "Probe invalidated path at node {}: cell({},{}) walkable={} surfaceY={}",
                      i, cx, cz,
                      cell ? cell->walkable : false,
                      cell ? cell->surfaceY : -1);
            invalidatePath();
            return;
        }
    }
}

void PatrolBehavior::updatePerception(float dt, NPCContext& ctx, const glm::vec3& forward) {
    if (!ctx.self || !ctx.entityRegistry) return;

    glm::vec3 pos = ctx.self->getPosition();

    // Wire voxel line-of-sight check if ChunkManager is available
    if (ctx.chunkManager && !m_perception.losCheck) {
        m_perception.losCheck = [cm = ctx.chunkManager](const glm::vec3& from, const glm::vec3& to) -> bool {
            glm::vec3 dir = to - from;
            float dist = glm::length(dir);
            if (dist < 0.01f) return true;
            dir /= dist;

            const float step = 0.5f;
            int steps = static_cast<int>(dist / step);
            for (int i = 1; i < steps; ++i) {
                glm::vec3 p = from + dir * (step * static_cast<float>(i));
                glm::ivec3 voxel(static_cast<int>(std::floor(p.x)),
                                 static_cast<int>(std::floor(p.y)),
                                 static_cast<int>(std::floor(p.z)));
                if (cm->hasVoxelAt(voxel)) {
                    return false;
                }
            }
            return true;
        };
    }

    // Wire debug cone visualization if RaycastVisualizer is available and enabled
    if (ctx.raycastVisualizer && ctx.raycastVisualizer->isEnabled() && !m_perception.debugConeDraw) {
        m_perception.debugConeDraw = [viz = ctx.raycastVisualizer](
            const glm::vec3& npcPos, const glm::vec3& npcForward,
            float range, float halfAngleDeg, bool hasThreat)
        {
            glm::vec3 eye = npcPos + glm::vec3(0.0f, 1.5f, 0.0f);
            glm::vec3 color = hasThreat ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 1.0f, 0.3f);

            glm::vec3 fwd2d(npcForward.x, 0.0f, npcForward.z);
            float fwdLen = glm::length(fwd2d);
            if (fwdLen > 0.001f) fwd2d /= fwdLen;
            else fwd2d = glm::vec3(0, 0, 1);

            float halfRad = glm::radians(halfAngleDeg);
            constexpr int NUM_SEGMENTS = 12;

            std::vector<glm::vec3> edgePoints;
            for (int i = 0; i <= NUM_SEGMENTS; ++i) {
                float t = static_cast<float>(i) / static_cast<float>(NUM_SEGMENTS);
                float angle = -halfRad + t * 2.0f * halfRad;
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);
                glm::vec3 dir(fwd2d.x * cosA + fwd2d.z * sinA,
                              0.0f,
                              -fwd2d.x * sinA + fwd2d.z * cosA);
                edgePoints.push_back(eye + dir * range);
            }

            for (int i = 0; i <= NUM_SEGMENTS; i += 3) {
                viz->addLine(eye, edgePoints[i], color);
            }
            for (int i = 0; i < NUM_SEGMENTS; ++i) {
                viz->addLine(edgePoints[i], edgePoints[i + 1], color);
            }

            glm::vec3 fwdColor = hasThreat ? glm::vec3(1.0f, 0.5f, 0.0f) : glm::vec3(0.5f, 1.0f, 0.5f);
            viz->addLine(eye, eye + fwd2d * range, fwdColor);
        };
    } else if (ctx.raycastVisualizer && !ctx.raycastVisualizer->isEnabled()) {
        m_perception.debugConeDraw = nullptr;
    }

    m_perception.update(dt, pos, forward, ctx.selfId, *ctx.entityRegistry);
}

} // namespace Scene
} // namespace Phyxel
