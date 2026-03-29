#pragma once

#include "scene/NPCBehavior.h"
#include "ai/Blackboard.h"
#include "ai/PerceptionSystem.h"
#include "ai/BehaviorTree.h"
#include "ai/UtilityAI.h"
#include <memory>

namespace Phyxel {
namespace Scene {

/// NPCBehavior adapter that drives NPC actions via a UtilityBrain 
/// (or a plain BehaviorTree). Bridges the engine's NPCBehavior interface
/// to the AI subsystem.
class BehaviorTreeBehavior : public NPCBehavior {
public:
    /// Default construct (empty — use setters to attach brain/tree later).
    BehaviorTreeBehavior() = default;

    /// Construct with a UtilityBrain (recommended — picks best action automatically).
    explicit BehaviorTreeBehavior(std::shared_ptr<AI::UtilityBrain> brain);

    /// Construct with a single BehaviorTree (always runs the same tree).
    explicit BehaviorTreeBehavior(AI::BTNodePtr rootTree);

    void update(float dt, NPCContext& ctx) override;
    void onInteract(Entity* interactor) override;
    void onEvent(const std::string& eventType, const nlohmann::json& data) override;
    std::string getBehaviorName() const override { return "BehaviorTree"; }

    // Access subsystems for inspection / MCP tools
    AI::Blackboard& getBlackboard() { return m_blackboard; }
    const AI::Blackboard& getBlackboard() const { return m_blackboard; }
    AI::PerceptionComponent& getPerception() { return m_perception; }
    const AI::PerceptionComponent& getPerception() const { return m_perception; }
    AI::UtilityBrain* getUtilityBrain() { return m_brain.get(); }
    AI::BehaviorTree* getTree() { return m_tree.get(); }

    /// Set or replace the behavior tree root node.
    void setTree(AI::BTNodePtr rootNode) {
        m_tree = std::make_unique<AI::BehaviorTree>(std::move(rootNode));
    }

private:
    AI::Blackboard m_blackboard;
    AI::PerceptionComponent m_perception;
    std::shared_ptr<AI::UtilityBrain> m_brain;        // null if using raw tree
    std::unique_ptr<AI::BehaviorTree> m_tree;          // null if using brain
};

} // namespace Scene
} // namespace Phyxel
