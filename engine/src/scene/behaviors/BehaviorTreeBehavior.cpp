#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/Entity.h"
#include "core/EntityRegistry.h"
#include "core/ChunkManager.h"
#include "graphics/RaycastVisualizer.h"
#include "ui/SpeechBubbleManager.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Phyxel {
namespace Scene {

BehaviorTreeBehavior::BehaviorTreeBehavior(std::shared_ptr<AI::UtilityBrain> brain)
    : m_brain(std::move(brain)) {}

BehaviorTreeBehavior::BehaviorTreeBehavior(AI::BTNodePtr rootTree)
    : m_tree(std::make_unique<AI::BehaviorTree>(std::move(rootTree))) {}

void BehaviorTreeBehavior::update(float dt, NPCContext& ctx) {
    // Update perception
    if (ctx.self && ctx.entityRegistry) {
        glm::vec3 pos = ctx.self->getPosition();
        // Derive forward from entity rotation (model faces +Z; atan2(x,z) yaw convention)
        glm::vec3 forward = ctx.self->getRotation() * glm::vec3(0.0f, 0.0f, 1.0f);

        // Wire voxel line-of-sight check if ChunkManager is available
        if (ctx.chunkManager && !m_perception.losCheck) {
            m_perception.losCheck = [cm = ctx.chunkManager](const glm::vec3& from, const glm::vec3& to) -> bool {
                // Simple DDA-style ray march checking each voxel along the ray
                glm::vec3 dir = to - from;
                float dist = glm::length(dir);
                if (dist < 0.01f) return true;
                dir /= dist;

                // Step size slightly less than 1 voxel to avoid skipping
                const float step = 0.5f;
                int steps = static_cast<int>(dist / step);
                for (int i = 1; i < steps; ++i) {
                    glm::vec3 p = from + dir * (step * static_cast<float>(i));
                    glm::ivec3 voxel(static_cast<int>(std::floor(p.x)),
                                     static_cast<int>(std::floor(p.y)),
                                     static_cast<int>(std::floor(p.z)));
                    if (cm->hasVoxelAt(voxel)) {
                        return false; // Blocked by solid voxel
                    }
                }
                return true; // Clear LOS
            };
        }

        // Wire debug cone visualization if RaycastVisualizer is available and enabled
        if (ctx.raycastVisualizer && ctx.raycastVisualizer->isEnabled() && !m_perception.debugConeDraw) {
            m_perception.debugConeDraw = [viz = ctx.raycastVisualizer](
                const glm::vec3& npcPos, const glm::vec3& npcForward,
                float range, float halfAngleDeg, bool hasThreat)
            {
                // Draw FOV cone as a wireframe: lines from eye to cone edge
                glm::vec3 eye = npcPos + glm::vec3(0.0f, 1.5f, 0.0f); // eye height
                glm::vec3 color = hasThreat ? glm::vec3(1.0f, 0.2f, 0.2f) : glm::vec3(0.2f, 1.0f, 0.3f);

                // Normalize forward on XZ plane
                glm::vec3 fwd2d(npcForward.x, 0.0f, npcForward.z);
                float fwdLen = glm::length(fwd2d);
                if (fwdLen > 0.001f) fwd2d /= fwdLen;
                else fwd2d = glm::vec3(0, 0, -1);

                float halfRad = glm::radians(halfAngleDeg);
                constexpr int NUM_SEGMENTS = 12;

                // Generate cone edge rays by rotating forward around Y axis
                std::vector<glm::vec3> edgePoints;
                for (int i = 0; i <= NUM_SEGMENTS; ++i) {
                    float t = static_cast<float>(i) / static_cast<float>(NUM_SEGMENTS);
                    float angle = -halfRad + t * 2.0f * halfRad;
                    float cosA = std::cos(angle);
                    float sinA = std::sin(angle);
                    // Rotate fwd2d around Y
                    glm::vec3 dir(fwd2d.x * cosA + fwd2d.z * sinA,
                                  0.0f,
                                  -fwd2d.x * sinA + fwd2d.z * cosA);
                    edgePoints.push_back(eye + dir * range);
                }

                // Draw radial lines from eye to cone edge
                for (int i = 0; i <= NUM_SEGMENTS; i += 3) {
                    viz->addLine(eye, edgePoints[i], color);
                }

                // Draw arc along the cone rim
                for (int i = 0; i < NUM_SEGMENTS; ++i) {
                    viz->addLine(edgePoints[i], edgePoints[i + 1], color);
                }

                // Draw center line (forward direction)
                glm::vec3 fwdColor = hasThreat ? glm::vec3(1.0f, 0.5f, 0.0f) : glm::vec3(0.5f, 1.0f, 0.5f);
                viz->addLine(eye, eye + fwd2d * range, fwdColor);
            };
        } else if (ctx.raycastVisualizer && !ctx.raycastVisualizer->isEnabled()) {
            // Clear the cone draw callback when viz is disabled
            m_perception.debugConeDraw = nullptr;
        }

        m_perception.update(dt, pos, forward, ctx.selfId, *ctx.entityRegistry);
    }

    // Write perception results to blackboard for tree nodes to read
    auto threats = m_perception.getThreats(0.5f);
    m_blackboard.set("hasThreat", !threats.empty());
    if (!threats.empty()) {
        m_blackboard.set("threat", threats.front().entityId);
        m_blackboard.set("threatPos", threats.front().position);
    }

    auto visible = m_perception.getVisibleEntities();
    m_blackboard.set("visibleCount", static_cast<int>(visible.size()));

    // Build ActionContext from NPCContext
    AI::ActionContext actCtx;
    actCtx.self = ctx.self;
    actCtx.selfId = ctx.selfId;
    actCtx.entityRegistry = ctx.entityRegistry;
    actCtx.speechBubbleManager = ctx.speechBubbleManager;
    actCtx.blackboard = &m_blackboard;

    // Tick the brain or tree
    if (m_brain) {
        m_brain->tick(dt, actCtx);
    } else if (m_tree) {
        m_tree->tick(dt, actCtx);
    }
}

void BehaviorTreeBehavior::onInteract(Entity* interactor) {
    m_blackboard.set("interacted", true);
    if (interactor) {
        m_blackboard.set("interactor", interactor->getPosition());
    }
}

void BehaviorTreeBehavior::onEvent(const std::string& eventType, const nlohmann::json& data) {
    m_blackboard.set("lastEvent", eventType);
    // Store event-specific data as blackboard keys
    if (eventType == "attacked") {
        m_blackboard.set("wasAttacked", true);
        if (data.contains("attackerId")) {
            m_blackboard.set("attacker", data["attackerId"].get<std::string>());
        }
    }
}

} // namespace Scene
} // namespace Phyxel
