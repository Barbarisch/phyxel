#pragma once

#include "ai/ActionSystem.h"
#include "ai/Blackboard.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

// ============================================================================
// BTStatus — result of a single tick
// ============================================================================
enum class BTStatus {
    Success,
    Failure,
    Running
};

// ============================================================================
// BTNode — abstract base for all behavior tree nodes
// ============================================================================
class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus tick(float dt, ActionContext& ctx) = 0;
    virtual void reset() {}
    virtual std::string getTypeName() const = 0;

    const std::string& getName() const { return m_name; }
    void setName(const std::string& name) { m_name = name; }

protected:
    std::string m_name;
};

using BTNodePtr = std::shared_ptr<BTNode>;

// ============================================================================
// COMPOSITE NODES
// ============================================================================

/// Sequence — runs children in order; fails on first failure.
class SequenceNode : public BTNode {
public:
    SequenceNode& child(BTNodePtr node) { m_children.push_back(std::move(node)); return *this; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        while (m_index < m_children.size()) {
            auto status = m_children[m_index]->tick(dt, ctx);
            if (status == BTStatus::Running) return BTStatus::Running;
            if (status == BTStatus::Failure) { reset(); return BTStatus::Failure; }
            ++m_index;
        }
        reset();
        return BTStatus::Success;
    }

    void reset() override {
        m_index = 0;
        for (auto& c : m_children) c->reset();
    }

    std::string getTypeName() const override { return "Sequence"; }
    const std::vector<BTNodePtr>& getChildren() const { return m_children; }

private:
    std::vector<BTNodePtr> m_children;
    size_t m_index = 0;
};

/// Selector — runs children in order; succeeds on first success.
class SelectorNode : public BTNode {
public:
    SelectorNode& child(BTNodePtr node) { m_children.push_back(std::move(node)); return *this; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        while (m_index < m_children.size()) {
            auto status = m_children[m_index]->tick(dt, ctx);
            if (status == BTStatus::Running) return BTStatus::Running;
            if (status == BTStatus::Success) { reset(); return BTStatus::Success; }
            ++m_index;
        }
        reset();
        return BTStatus::Failure;
    }

    void reset() override {
        m_index = 0;
        for (auto& c : m_children) c->reset();
    }

    std::string getTypeName() const override { return "Selector"; }
    const std::vector<BTNodePtr>& getChildren() const { return m_children; }

private:
    std::vector<BTNodePtr> m_children;
    size_t m_index = 0;
};

/// Parallel — runs all children simultaneously; succeeds when required count succeed.
class ParallelNode : public BTNode {
public:
    explicit ParallelNode(int requiredSuccesses = -1)
        : m_requiredSuccesses(requiredSuccesses) {}

    ParallelNode& child(BTNodePtr node) { m_children.push_back(std::move(node)); return *this; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        int successes = 0, failures = 0;
        int required = (m_requiredSuccesses <= 0)
                          ? static_cast<int>(m_children.size())
                          : m_requiredSuccesses;

        for (auto& c : m_children) {
            auto status = c->tick(dt, ctx);
            if (status == BTStatus::Success) ++successes;
            else if (status == BTStatus::Failure) ++failures;
        }

        if (successes >= required) return BTStatus::Success;
        if (failures > static_cast<int>(m_children.size()) - required) return BTStatus::Failure;
        return BTStatus::Running;
    }

    void reset() override {
        for (auto& c : m_children) c->reset();
    }

    std::string getTypeName() const override { return "Parallel"; }

private:
    std::vector<BTNodePtr> m_children;
    int m_requiredSuccesses;
};

// ============================================================================
// DECORATOR NODES
// ============================================================================

/// Inverter — flips Success/Failure of child.
class InverterNode : public BTNode {
public:
    explicit InverterNode(BTNodePtr child) : m_child(std::move(child)) {}

    BTStatus tick(float dt, ActionContext& ctx) override {
        auto status = m_child->tick(dt, ctx);
        if (status == BTStatus::Success) return BTStatus::Failure;
        if (status == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }

    void reset() override { m_child->reset(); }
    std::string getTypeName() const override { return "Inverter"; }

private:
    BTNodePtr m_child;
};

/// Repeater — repeats child N times (or forever if count <= 0).
class RepeaterNode : public BTNode {
public:
    RepeaterNode(BTNodePtr child, int count = 0)
        : m_child(std::move(child)), m_count(count) {}

    BTStatus tick(float dt, ActionContext& ctx) override {
        auto status = m_child->tick(dt, ctx);
        if (status == BTStatus::Running) return BTStatus::Running;

        m_child->reset();
        ++m_iteration;

        if (m_count > 0 && m_iteration >= m_count) {
            m_iteration = 0;
            return BTStatus::Success;
        }
        return BTStatus::Running; // Keep repeating
    }

    void reset() override { m_iteration = 0; m_child->reset(); }
    std::string getTypeName() const override { return "Repeater"; }

private:
    BTNodePtr m_child;
    int m_count;        // <= 0 means infinite
    int m_iteration = 0;
};

/// Cooldown — after child completes, blocks for cooldownSeconds.
class CooldownNode : public BTNode {
public:
    CooldownNode(BTNodePtr child, float cooldownSeconds)
        : m_child(std::move(child)), m_cooldown(cooldownSeconds) {}

    BTStatus tick(float dt, ActionContext& ctx) override {
        if (m_timer > 0.0f) {
            m_timer -= dt;
            return BTStatus::Failure; // On cooldown
        }
        auto status = m_child->tick(dt, ctx);
        if (status != BTStatus::Running) {
            m_timer = m_cooldown;
            m_child->reset();
        }
        return status;
    }

    void reset() override { m_timer = 0.0f; m_child->reset(); }
    std::string getTypeName() const override { return "Cooldown"; }

private:
    BTNodePtr m_child;
    float m_cooldown;
    float m_timer = 0.0f;
};

/// Succeeder — always returns Success (wraps child for side effects).
class SucceederNode : public BTNode {
public:
    explicit SucceederNode(BTNodePtr child) : m_child(std::move(child)) {}

    BTStatus tick(float dt, ActionContext& ctx) override {
        m_child->tick(dt, ctx);
        return BTStatus::Success;
    }

    void reset() override { m_child->reset(); }
    std::string getTypeName() const override { return "Succeeder"; }

private:
    BTNodePtr m_child;
};

// ============================================================================
// LEAF NODES
// ============================================================================

/// ActionNode — wraps an NPCAction. start() on first tick, update() each tick.
class ActionNode : public BTNode {
public:
    explicit ActionNode(std::shared_ptr<NPCAction> action)
        : m_action(std::move(action)) { m_name = m_action->getName(); }

    BTStatus tick(float dt, ActionContext& ctx) override {
        if (!m_started) {
            m_action->start(ctx);
            m_started = true;
        }
        auto status = m_action->update(dt, ctx);
        switch (status) {
            case ActionStatus::Success: reset(); return BTStatus::Success;
            case ActionStatus::Failure: reset(); return BTStatus::Failure;
            default: return BTStatus::Running;
        }
    }

    void reset() override { m_started = false; }
    std::string getTypeName() const override { return "Action"; }

private:
    std::shared_ptr<NPCAction> m_action;
    bool m_started = false;
};

/// ConditionNode — evaluates a predicate. Success if true, Failure if false.
class ConditionNode : public BTNode {
public:
    using Predicate = std::function<bool(ActionContext&)>;

    ConditionNode(Predicate pred, const std::string& name = "Condition")
        : m_predicate(std::move(pred)) { m_name = name; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        return m_predicate(ctx) ? BTStatus::Success : BTStatus::Failure;
    }

    std::string getTypeName() const override { return "Condition"; }

private:
    Predicate m_predicate;
};

/// BlackboardCondition — checks a blackboard bool key.
class BBConditionNode : public BTNode {
public:
    BBConditionNode(const std::string& key, bool expected = true)
        : m_key(key), m_expected(expected) { m_name = "BB:" + key; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        if (!ctx.blackboard) return BTStatus::Failure;
        return (ctx.blackboard->getBool(m_key) == m_expected)
                   ? BTStatus::Success : BTStatus::Failure;
    }

    std::string getTypeName() const override { return "BBCondition"; }

private:
    std::string m_key;
    bool m_expected;
};

/// BlackboardHasKey — checks if a blackboard key exists.
class BBHasKeyNode : public BTNode {
public:
    explicit BBHasKeyNode(const std::string& key) : m_key(key) { m_name = "HasKey:" + key; }

    BTStatus tick(float dt, ActionContext& ctx) override {
        if (!ctx.blackboard) return BTStatus::Failure;
        return ctx.blackboard->has(m_key) ? BTStatus::Success : BTStatus::Failure;
    }

    std::string getTypeName() const override { return "BBHasKey"; }

private:
    std::string m_key;
};

// ============================================================================
// Builder helpers — concise tree construction
// ============================================================================
namespace BT {

inline std::shared_ptr<SequenceNode> Sequence() { return std::make_shared<SequenceNode>(); }
inline std::shared_ptr<SelectorNode> Selector() { return std::make_shared<SelectorNode>(); }
inline std::shared_ptr<ParallelNode> Parallel(int req = -1) { return std::make_shared<ParallelNode>(req); }

inline BTNodePtr Inverter(BTNodePtr child) { return std::make_shared<InverterNode>(std::move(child)); }
inline BTNodePtr Repeater(BTNodePtr child, int n = 0) { return std::make_shared<RepeaterNode>(std::move(child), n); }
inline BTNodePtr Cooldown(BTNodePtr child, float sec) { return std::make_shared<CooldownNode>(std::move(child), sec); }
inline BTNodePtr Succeeder(BTNodePtr child) { return std::make_shared<SucceederNode>(std::move(child)); }

inline BTNodePtr Action(std::shared_ptr<NPCAction> a) { return std::make_shared<ActionNode>(std::move(a)); }

inline BTNodePtr Condition(ConditionNode::Predicate pred, const std::string& name = "Cond") {
    return std::make_shared<ConditionNode>(std::move(pred), name);
}
inline BTNodePtr BBCheck(const std::string& key, bool expected = true) {
    return std::make_shared<BBConditionNode>(key, expected);
}
inline BTNodePtr BBHasKey(const std::string& key) {
    return std::make_shared<BBHasKeyNode>(key);
}

} // namespace BT

// ============================================================================
// BehaviorTree — root container
// ============================================================================
class BehaviorTree {
public:
    BehaviorTree() = default;
    explicit BehaviorTree(BTNodePtr root) : m_root(std::move(root)) {}

    void setRoot(BTNodePtr root) { m_root = std::move(root); }

    BTStatus tick(float dt, ActionContext& ctx) {
        if (!m_root) return BTStatus::Failure;
        return m_root->tick(dt, ctx);
    }

    void reset() {
        if (m_root) m_root->reset();
    }

    BTNodePtr getRoot() const { return m_root; }

private:
    BTNodePtr m_root;
};

} // namespace AI
} // namespace Phyxel
