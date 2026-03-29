#include "scene/behaviors/BehaviorTreeBehavior.h"
#include "scene/Entity.h"
#include "core/EntityRegistry.h"
#include "ui/SpeechBubbleManager.h"
#include <glm/gtc/quaternion.hpp>

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
        // Derive forward from entity rotation (default forward is -Z in right-handed)
        glm::vec3 forward = ctx.self->getRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
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
