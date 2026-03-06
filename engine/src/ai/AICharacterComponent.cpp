#include "ai/AICharacterComponent.h"
#include "scene/Entity.h"
#include "scene/RagdollCharacter.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace AI {

// ============================================================================
// AIController
// ============================================================================

AIController::AIController(GooseBridge* bridge,
                           Scene::Entity* entity,
                           const std::string& entityId)
    : m_bridge(bridge)
    , m_entity(entity)
    , m_entityId(entityId)
{
    LOG_INFO("AI", "AIController created for entity '{}'", entityId);
}

AIController::~AIController() {
    stop();
}

// ============================================================================
// Configuration
// ============================================================================

void AIController::setRecipe(const std::string& recipePath) {
    m_recipePath = recipePath;
}

void AIController::setPersonality(const std::string& personality) {
    m_personality = personality;
}

void AIController::addSkill(const SkillConfig& skill) {
    // Replace if exists
    auto it = std::find_if(m_skills.begin(), m_skills.end(),
        [&](const SkillConfig& s) { return s.name == skill.name; });
    if (it != m_skills.end()) {
        *it = skill;
    } else {
        m_skills.push_back(skill);
    }
}

void AIController::removeSkill(const std::string& skillName) {
    m_skills.erase(
        std::remove_if(m_skills.begin(), m_skills.end(),
            [&](const SkillConfig& s) { return s.name == skillName; }),
        m_skills.end()
    );
}

// ============================================================================
// Lifecycle
// ============================================================================

bool AIController::start() {
    if (!m_bridge) {
        LOG_ERROR("AI", "AIController: no GooseBridge set for entity '{}'", m_entityId);
        return false;
    }

    if (!m_bridge->isServerRunning()) {
        LOG_ERROR("AI", "AIController: goose-server not running, cannot start entity '{}'",
                  m_entityId);
        return false;
    }

    // Create a session for this character
    m_sessionId = m_bridge->createSession(m_entityId, m_recipePath);
    if (m_sessionId.empty()) {
        LOG_ERROR("AI", "AIController: failed to create session for entity '{}'", m_entityId);
        return false;
    }

    // If we have a personality prompt, send it as the first message
    if (!m_personality.empty()) {
        std::string initMsg = "[System Initialization]\n" + m_personality;
        m_bridge->sendMessage(m_sessionId, initMsg);
    }

    m_state = AIState::Idle;
    m_timeSinceLastResponse = 0.0f;
    m_timeSinceLastPrompt = 0.0f;

    LOG_INFO("AI", "AIController: started entity '{}' with session {}", m_entityId, m_sessionId);
    return true;
}

void AIController::stop() {
    if (m_state == AIState::Disabled) return;

    // Wait for any pending replies
    for (auto& future : m_pendingReplies) {
        if (future.valid()) {
            future.wait_for(std::chrono::seconds(2));
        }
    }
    m_pendingReplies.clear();

    if (!m_sessionId.empty() && m_bridge) {
        m_bridge->destroySession(m_sessionId);
    }

    m_sessionId.clear();
    m_state = AIState::Disabled;
    m_moveTarget.reset();

    LOG_INFO("AI", "AIController: stopped entity '{}'", m_entityId);
}

void AIController::pause() {
    if (m_state != AIState::Disabled) {
        m_state = AIState::Idle;
        m_moveTarget.reset();
        LOG_DEBUG("AI", "AIController: paused entity '{}'", m_entityId);
    }
}

void AIController::resume() {
    if (m_state == AIState::Idle || m_state == AIState::Disabled) {
        m_state = AIState::Idle;
        LOG_DEBUG("AI", "AIController: resumed entity '{}'", m_entityId);
    }
}

// ============================================================================
// Game Loop
// ============================================================================

void AIController::update(float deltaTime) {
    if (m_state == AIState::Disabled) return;

    m_timeSinceLastResponse += deltaTime;
    m_timeSinceLastPrompt += deltaTime;

    // Clean up completed futures
    m_pendingReplies.erase(
        std::remove_if(m_pendingReplies.begin(), m_pendingReplies.end(),
            [](const std::future<bool>& f) {
                return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
            }),
        m_pendingReplies.end()
    );

    // Process pending commands for this entity
    for (const auto& cmd : m_pendingCommands) {
        processCommand(cmd);
    }
    m_pendingCommands.clear();

    // If we have an active move target, continue moving
    if (m_moveTarget.has_value() && m_entity) {
        glm::vec3 currentPos = m_entity->getPosition();
        glm::vec3 target = m_moveTarget.value();
        glm::vec3 diff = target - currentPos;
        float dist = glm::length(diff);

        if (dist < 0.5f) {
            // Arrived at destination
            m_moveTarget.reset();
            if (m_state == AIState::ExecutingAction) {
                m_state = AIState::Idle;
            }
        } else {
            // Move toward target via setControlInput if it's a RagdollCharacter
            auto* ragdoll = dynamic_cast<Scene::RagdollCharacter*>(m_entity);
            if (ragdoll) {
                // Calculate forward/turn from current orientation to target
                glm::vec3 dir = glm::normalize(diff);
                glm::vec3 forward = glm::vec3(0, 0, -1); // Default forward
                glm::quat rot = m_entity->getRotation();
                forward = rot * forward;

                float dotForward = glm::dot(
                    glm::normalize(glm::vec2(forward.x, forward.z)),
                    glm::normalize(glm::vec2(dir.x, dir.z))
                );
                float crossY = forward.x * dir.z - forward.z * dir.x;

                float turnInput = glm::clamp(crossY * 2.0f, -1.0f, 1.0f);
                float forwardInput = glm::clamp(dotForward, 0.0f, 1.0f) * m_moveSpeed;

                ragdoll->setControlInput(forwardInput, turnInput);
            }
        }
    }

    // If waiting too long, fall back to idle
    if (m_state == AIState::WaitingForAI &&
        m_timeSinceLastResponse > m_responseTimeout) {
        LOG_DEBUG("AI", "AIController: response timeout for entity '{}', going idle",
                  m_entityId);
        m_state = AIState::Idle;
    }

    // Update idle behavior
    if (m_state == AIState::Idle) {
        updateIdleBehavior(deltaTime);
    }
}

// ============================================================================
// Events
// ============================================================================

void AIController::notifyEvent(const std::string& eventType, const json& eventData) {
    if (m_state == AIState::Disabled || m_sessionId.empty()) return;

    // Rate-limit prompts
    if (m_timeSinceLastPrompt < m_minPromptInterval) {
        LOG_DEBUG("AI", "AIController: rate-limited event '{}' for entity '{}'",
                  eventType, m_entityId);
        return;
    }

    // Build context message
    json contextData = eventData;
    if (m_entity) {
        glm::vec3 pos = m_entity->getPosition();
        contextData["my_position"] = {
            {"x", pos.x}, {"y", pos.y}, {"z", pos.z}
        };
    }

    auto future = m_bridge->sendGameEvent(m_sessionId, eventType, contextData);
    m_pendingReplies.push_back(std::move(future));

    m_state = AIState::WaitingForAI;
    m_timeSinceLastPrompt = 0.0f;
}

void AIController::sendDirectMessage(const std::string& message) {
    if (m_state == AIState::Disabled || m_sessionId.empty()) return;

    auto future = m_bridge->sendMessage(m_sessionId, message);
    m_pendingReplies.push_back(std::move(future));

    m_state = AIState::WaitingForAI;
    m_timeSinceLastPrompt = 0.0f;
}

// ============================================================================
// State Queries
// ============================================================================

float AIController::getTimeSinceLastResponse() const {
    return m_timeSinceLastResponse;
}

// ============================================================================
// Command Processing
// ============================================================================

void AIController::processCommand(const AICommand& cmd) {
    std::visit([this](const auto& c) {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, MoveToCommand>) {
            if (c.entityId == m_entityId) executeMoveToCommand(c);
        }
        else if constexpr (std::is_same_v<T, SayDialogCommand>) {
            if (c.entityId == m_entityId) executeDialogCommand(c);
        }
        else if constexpr (std::is_same_v<T, PlayAnimationCommand>) {
            if (c.entityId == m_entityId) executeAnimationCommand(c);
        }
        else if constexpr (std::is_same_v<T, AttackCommand>) {
            if (c.entityId == m_entityId) executeAttackCommand(c);
        }
        else if constexpr (std::is_same_v<T, EmoteCommand>) {
            if (c.entityId == m_entityId) executeEmoteCommand(c);
        }
        // SpawnEntityCommand, SetQuestStateCommand, TriggerEventCommand
        // are handled globally by the AICharacterManager, not per-controller
    }, cmd);

    m_timeSinceLastResponse = 0.0f;
    m_state = AIState::ExecutingAction;
}

void AIController::executeMoveToCommand(const MoveToCommand& cmd) {
    m_moveTarget = cmd.target;
    m_moveSpeed = cmd.speed;
    LOG_DEBUG("AI", "AIController [{}]: moving to ({}, {}, {})",
              m_entityId, cmd.target.x, cmd.target.y, cmd.target.z);
}

void AIController::executeDialogCommand(const SayDialogCommand& cmd) {
    LOG_INFO("AI", "AIController [{}] says [{}]: {}", m_entityId, cmd.emotion, cmd.text);

    if (m_dialogHandler) {
        m_dialogHandler(cmd.text, cmd.emotion);
    }
}

void AIController::executeAnimationCommand(const PlayAnimationCommand& cmd) {
    LOG_DEBUG("AI", "AIController [{}]: playing animation '{}'", m_entityId, cmd.animationName);

    if (m_animHandler) {
        m_animHandler(cmd.animationName, cmd.loop);
    }
}

void AIController::executeAttackCommand(const AttackCommand& cmd) {
    LOG_DEBUG("AI", "AIController [{}]: attacking {} with '{}'",
              m_entityId, cmd.targetId, cmd.skillName);

    if (m_attackHandler) {
        m_attackHandler(cmd.targetId, cmd.skillName);
    }
}

void AIController::executeEmoteCommand(const EmoteCommand& cmd) {
    LOG_DEBUG("AI", "AIController [{}]: emoting '{}'", m_entityId, cmd.emoteType);

    if (m_emoteHandler) {
        m_emoteHandler(cmd.emoteType);
    }
}

void AIController::updateIdleBehavior(float deltaTime) {
    // Default idle: just stand in place
    // Subclasses or handlers can override for patrol, look-around, etc.
    auto* ragdoll = dynamic_cast<Scene::RagdollCharacter*>(m_entity);
    if (ragdoll && !m_moveTarget.has_value()) {
        ragdoll->setControlInput(0.0f, 0.0f);  // Stand still
    }
}

// ============================================================================
// AICharacterManager
// ============================================================================

AICharacterManager::AICharacterManager(GooseBridge* bridge)
    : m_bridge(bridge)
{
}

AIController* AICharacterManager::createController(
    Scene::Entity* entity,
    const std::string& entityId,
    const std::string& recipePath)
{
    auto controller = std::make_unique<AIController>(m_bridge, entity, entityId);
    if (!recipePath.empty()) {
        controller->setRecipe(recipePath);
    }

    auto* ptr = controller.get();
    m_controllers[entityId] = std::move(controller);
    return ptr;
}

void AICharacterManager::removeController(const std::string& entityId) {
    auto it = m_controllers.find(entityId);
    if (it != m_controllers.end()) {
        it->second->stop();
        m_controllers.erase(it);
    }
}

AIController* AICharacterManager::getController(const std::string& entityId) {
    auto it = m_controllers.find(entityId);
    return it != m_controllers.end() ? it->second.get() : nullptr;
}

void AICharacterManager::update(float deltaTime) {
    // First, distribute commands from the global queue to individual controllers
    distributeCommands();

    // Then update all controllers
    for (auto& [id, controller] : m_controllers) {
        controller->update(deltaTime);
    }
}

void AICharacterManager::broadcastEvent(const std::string& eventType,
                                         const json& eventData) {
    for (auto& [id, controller] : m_controllers) {
        if (controller->isActive()) {
            controller->notifyEvent(eventType, eventData);
        }
    }
}

std::vector<std::string> AICharacterManager::getActiveEntityIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, controller] : m_controllers) {
        if (controller->isActive()) {
            ids.push_back(id);
        }
    }
    return ids;
}

void AICharacterManager::startAll() {
    for (auto& [id, controller] : m_controllers) {
        if (!controller->isActive()) {
            controller->start();
        }
    }
}

void AICharacterManager::stopAll() {
    for (auto& [id, controller] : m_controllers) {
        controller->stop();
    }
}

void AICharacterManager::distributeCommands() {
    // Drain all commands from the global queue
    std::vector<AICommand> commands;
    m_bridge->getCommandQueue().drainCommands(commands);

    if (commands.empty()) return;

    // Distribute commands to the appropriate controllers
    for (const auto& cmd : commands) {
        std::visit([this](const auto& c) {
            using T = std::decay_t<decltype(c)>;

            // Entity-targeted commands go to specific controllers
            if constexpr (std::is_same_v<T, MoveToCommand> ||
                          std::is_same_v<T, SayDialogCommand> ||
                          std::is_same_v<T, PlayAnimationCommand> ||
                          std::is_same_v<T, AttackCommand> ||
                          std::is_same_v<T, EmoteCommand>) {
                auto* controller = getController(c.entityId);
                if (controller) {
                    controller->m_pendingCommands.push_back(c);
                } else {
                    LOG_WARN("AI", "AICharacterManager: no controller for entity '{}'",
                             c.entityId);
                }
            }
            // Global commands are handled here
            else if constexpr (std::is_same_v<T, SpawnEntityCommand>) {
                LOG_INFO("AI", "AICharacterManager: spawn entity '{}' template '{}' at ({},{},{})",
                         c.assignedId, c.templateName,
                         c.position.x, c.position.y, c.position.z);
                // TODO: Integrate with Application::createEntity()
            }
            else if constexpr (std::is_same_v<T, SetQuestStateCommand>) {
                LOG_INFO("AI", "AICharacterManager: quest '{}' → state '{}'",
                         c.questId, c.state);
                // TODO: Integrate with quest system
            }
            else if constexpr (std::is_same_v<T, TriggerEventCommand>) {
                LOG_INFO("AI", "AICharacterManager: event '{}' payload: {}",
                         c.eventName, c.payload);
                // TODO: Integrate with event bus
            }
        }, cmd);
    }
}

} // namespace AI
} // namespace Phyxel
