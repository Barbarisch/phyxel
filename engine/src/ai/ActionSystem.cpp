#include "ai/ActionSystem.h"
#include "ai/Blackboard.h"
#include "core/EntityRegistry.h"
#include "scene/Entity.h"
#include "ui/SpeechBubbleManager.h"

#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace Phyxel {
namespace AI {

// ============================================================================
// MoveToAction
// ============================================================================

void MoveToAction::start(ActionContext& ctx) {
    // Nothing special — will start moving in update
}

ActionStatus MoveToAction::update(float dt, ActionContext& ctx) {
    if (!ctx.self) return ActionStatus::Failure;

    glm::vec3 pos = ctx.self->getPosition();
    glm::vec3 diff = m_target - pos;
    float distXZ = glm::length(glm::vec2(diff.x, diff.z));

    if (distXZ < m_threshold) {
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return ActionStatus::Success;
    }

    glm::vec3 direction = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
    ctx.self->setMoveVelocity(direction * m_speed);

    // Face movement direction
    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.01f) {
        float yaw = glm::degrees(atan2(direction.x, direction.z));
        ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));
    }

    return ActionStatus::Running;
}

void MoveToAction::abort(ActionContext& ctx) {
    if (ctx.self) ctx.self->setMoveVelocity(glm::vec3(0.0f));
}

// ============================================================================
// LookAtAction
// ============================================================================

ActionStatus LookAtAction::update(float dt, ActionContext& ctx) {
    if (!ctx.self) return ActionStatus::Failure;

    glm::vec3 diff = m_target - ctx.self->getPosition();
    float len = glm::length(glm::vec2(diff.x, diff.z));
    if (len < 0.01f) return ActionStatus::Success;

    float yaw = glm::degrees(atan2(diff.x, diff.z));
    ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));
    return ActionStatus::Success;
}

// ============================================================================
// WaitAction
// ============================================================================

ActionStatus WaitAction::update(float dt, ActionContext& ctx) {
    m_elapsed += dt;
    if (m_elapsed >= m_duration) return ActionStatus::Success;
    // Keep NPC still while waiting
    if (ctx.self) ctx.self->setMoveVelocity(glm::vec3(0.0f));
    return ActionStatus::Running;
}

// ============================================================================
// SpeakAction
// ============================================================================

ActionStatus SpeakAction::update(float dt, ActionContext& ctx) {
    if (ctx.speechBubbleManager && !ctx.selfId.empty()) {
        ctx.speechBubbleManager->say(ctx.selfId, m_text, m_duration);
    }
    return ActionStatus::Success;
}

// ============================================================================
// SetBlackboardAction
// ============================================================================

ActionStatus SetBlackboardAction::update(float dt, ActionContext& ctx) {
    if (ctx.blackboard) {
        ctx.blackboard->setString(m_key, m_value);
    }
    return ActionStatus::Success;
}

// ============================================================================
// MoveToEntityAction
// ============================================================================

ActionStatus MoveToEntityAction::update(float dt, ActionContext& ctx) {
    if (!ctx.self || !ctx.blackboard || !ctx.entityRegistry) return ActionStatus::Failure;

    std::string targetId = ctx.blackboard->getString(m_bbKey);
    if (targetId.empty()) return ActionStatus::Failure;

    auto* target = ctx.entityRegistry->getEntity(targetId);
    if (!target) return ActionStatus::Failure;

    glm::vec3 pos = ctx.self->getPosition();
    glm::vec3 targetPos = target->getPosition();
    glm::vec3 diff = targetPos - pos;
    float distXZ = glm::length(glm::vec2(diff.x, diff.z));

    if (distXZ < m_threshold) {
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return ActionStatus::Success;
    }

    glm::vec3 direction = glm::normalize(glm::vec3(diff.x, 0.0f, diff.z));
    ctx.self->setMoveVelocity(direction * m_speed);

    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.01f) {
        float yaw = glm::degrees(atan2(direction.x, direction.z));
        ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));
    }

    return ActionStatus::Running;
}

// ============================================================================
// FleeAction
// ============================================================================

ActionStatus FleeAction::update(float dt, ActionContext& ctx) {
    if (!ctx.self) return ActionStatus::Failure;

    m_elapsed += dt;
    if (m_elapsed >= m_duration) {
        ctx.self->setMoveVelocity(glm::vec3(0.0f));
        return ActionStatus::Success;
    }

    // Get threat position from blackboard or just run in current facing direction
    glm::vec3 threatPos = ctx.self->getPosition();
    if (ctx.blackboard && ctx.entityRegistry) {
        std::string threatId = ctx.blackboard->getString(m_bbKey);
        if (!threatId.empty()) {
            auto* threat = ctx.entityRegistry->getEntity(threatId);
            if (threat) threatPos = threat->getPosition();
        }
    }

    glm::vec3 pos = ctx.self->getPosition();
    glm::vec3 away = pos - threatPos;
    float len = glm::length(glm::vec2(away.x, away.z));
    glm::vec3 direction;
    if (len > 0.01f) {
        direction = glm::normalize(glm::vec3(away.x, 0.0f, away.z));
    } else {
        direction = glm::vec3(0, 0, 1); // Default flee direction
    }

    ctx.self->setMoveVelocity(direction * m_speed);

    // Face away from threat
    float yaw = glm::degrees(atan2(direction.x, direction.z));
    ctx.self->setRotation(glm::angleAxis(glm::radians(yaw), glm::vec3(0, 1, 0)));

    return ActionStatus::Running;
}

} // namespace AI
} // namespace Phyxel
