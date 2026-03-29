#pragma once

#include "ai/BehaviorTree.h"
#include "ai/Blackboard.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace AI {

// ============================================================================
// Consideration — a single scoring axis (0..1)
// ============================================================================
class Consideration {
public:
    using ScoreFunc = std::function<float(ActionContext&)>;

    Consideration(const std::string& name, ScoreFunc scorer, float weight = 1.0f)
        : m_name(name), m_scorer(std::move(scorer)), m_weight(weight) {}

    float evaluate(ActionContext& ctx) const {
        float raw = m_scorer(ctx);
        // Clamp to [0, 1]
        return std::max(0.0f, std::min(1.0f, raw)) * m_weight;
    }

    const std::string& getName() const { return m_name; }
    float getWeight() const { return m_weight; }

private:
    std::string m_name;
    ScoreFunc m_scorer;
    float m_weight;
};

// ============================================================================
// UtilityAction — a behavior tree paired with scoring considerations
// ============================================================================
class UtilityAction {
public:
    UtilityAction(const std::string& name, BTNodePtr tree)
        : m_name(name), m_tree(std::move(tree)) {}

    UtilityAction& addConsideration(const std::string& name, Consideration::ScoreFunc scorer, float weight = 1.0f) {
        m_considerations.emplace_back(name, std::move(scorer), weight);
        return *this;
    }

    /// Multiplicative scoring — each consideration multiplies the total.
    float evaluate(ActionContext& ctx) const {
        if (m_considerations.empty()) return 0.0f;
        float score = 1.0f;
        for (auto& c : m_considerations) {
            score *= c.evaluate(ctx);
        }
        return score * m_bonus;
    }

    BTStatus tick(float dt, ActionContext& ctx) {
        if (!m_tree) return BTStatus::Failure;
        return m_tree->tick(dt, ctx);
    }

    void reset() {
        if (m_tree) m_tree->reset();
    }

    const std::string& getName() const { return m_name; }
    const std::vector<Consideration>& getConsiderations() const { return m_considerations; }
    void setBonus(float b) { m_bonus = b; }
    float getBonus() const { return m_bonus; }

private:
    std::string m_name;
    BTNodePtr m_tree;
    std::vector<Consideration> m_considerations;
    float m_bonus = 1.0f;  // Multiplied into final score
};

// ============================================================================
// UtilityBrain — evaluates all actions, picks highest, runs it
// ============================================================================
class UtilityBrain {
public:
    void addAction(std::shared_ptr<UtilityAction> action) {
        m_actions.push_back(std::move(action));
    }

    BTStatus tick(float dt, ActionContext& ctx) {
        // Re-evaluate periodically or when current action finishes
        m_evalTimer -= dt;
        if (!m_current || m_evalTimer <= 0.0f) {
            selectBest(ctx);
            m_evalTimer = m_evalInterval;
        }

        if (!m_current) return BTStatus::Failure;

        auto status = m_current->tick(dt, ctx);
        if (status != BTStatus::Running) {
            m_current->reset();
            m_current = nullptr;
        }
        return status;
    }

    void reset() {
        if (m_current) { m_current->reset(); m_current = nullptr; }
        m_evalTimer = 0.0f;
    }

    void setEvalInterval(float seconds) { m_evalInterval = seconds; }

    // Inspection
    const std::string& getCurrentActionName() const {
        static const std::string empty;
        return m_current ? m_current->getName() : empty;
    }

    nlohmann::json toJson(ActionContext& ctx) const {
        nlohmann::json j;
        j["currentAction"] = m_current ? m_current->getName() : "none";
        j["evalInterval"] = m_evalInterval;
        nlohmann::json scores = nlohmann::json::array();
        for (auto& a : m_actions) {
            nlohmann::json entry;
            entry["name"] = a->getName();
            entry["score"] = a->evaluate(ctx);
            scores.push_back(entry);
        }
        j["scores"] = scores;
        return j;
    }

    const std::vector<std::shared_ptr<UtilityAction>>& getActions() const { return m_actions; }

private:
    void selectBest(ActionContext& ctx) {
        std::shared_ptr<UtilityAction> best;
        float bestScore = -1.0f;

        for (auto& a : m_actions) {
            float score = a->evaluate(ctx);
            if (score > bestScore) {
                bestScore = score;
                best = a;
            }
        }

        // Switch action if a better one is found
        if (best != m_current) {
            if (m_current) m_current->reset();
            m_current = best;
        }
    }

    std::vector<std::shared_ptr<UtilityAction>> m_actions;
    std::shared_ptr<UtilityAction> m_current;
    float m_evalInterval = 1.0f;  // Re-evaluate every second
    float m_evalTimer = 0.0f;
};

} // namespace AI
} // namespace Phyxel
