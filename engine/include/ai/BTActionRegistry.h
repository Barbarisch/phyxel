#pragma once

#include "ai/ActionSystem.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace AI {

/// Host-registered behavior-tree ACTIONS — the seam that lets a GAME add new
/// AI verbs without modifying (or recompiling) the engine.
///
/// The BT loader used to own a fixed vocabulary: MoveTo, LookAt, Wait, Speak,
/// Flee and friends, hardcoded in a chain inside BTLoader::parseAction. You
/// could rearrange those verbs from JSON but never add one, so every new idea
/// ("a caster that holds range and fires on cooldown") meant an engine change
/// and a full rebuild. That is backwards: a game should be able to extend the
/// engine's vocabulary, not edit the engine.
///
/// A game registers factories at startup:
///
///     auto& reg = AI::BTActionRegistry::instance();
///     reg.add("hold_range", [](const nlohmann::json& p) {
///         const float want = p.value("range", 12.0f);
///         return AI::makeAction("hold_range",
///             [want](float dt, AI::ActionContext& ctx) { ... return AI::ActionStatus::Running; });
///     });
///
/// and then authors behavior purely in JSON:
///
///     {"type":"Selector","children":[
///        {"type":"Action","action":"flee_below","hp":0.3},
///        {"type":"Action","action":"hold_range","range":14},
///        {"type":"Action","action":"cast_spell","spell":"fire_bolt","cooldown":2.0}]}
///
/// Registered names are consulted BEFORE the built-ins, so a game may also
/// override a stock action with its own version.
class BTActionRegistry {
public:
    /// Builds an action instance from the node's JSON (its params). Returning
    /// null makes the loader fall through to the built-in vocabulary.
    using Factory = std::function<std::shared_ptr<NPCAction>(const nlohmann::json&)>;

    static BTActionRegistry& instance() {
        static BTActionRegistry s;
        return s;
    }

    void add(const std::string& actionName, Factory factory) {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_factories[actionName] = std::move(factory);
    }

    bool has(const std::string& actionName) const {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_factories.count(actionName) != 0;
    }

    /// Build a registered action, or null when the name is unknown.
    std::shared_ptr<NPCAction> create(const std::string& actionName,
                                      const nlohmann::json& params) const {
        Factory f;
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            auto it = m_factories.find(actionName);
            if (it == m_factories.end()) return nullptr;
            f = it->second;
        }
        return f ? f(params) : nullptr;
    }

    /// Every registered name — for tooling, docs, and "unknown action" errors
    /// that can suggest what IS available.
    std::vector<std::string> names() const {
        std::lock_guard<std::mutex> lk(m_mutex);
        std::vector<std::string> out;
        out.reserve(m_factories.size());
        for (const auto& [k, _] : m_factories) out.push_back(k);
        return out;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_factories.clear();
    }

private:
    BTActionRegistry() = default;
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, Factory> m_factories;
};

// ============================================================================
// LambdaAction — an action from a callable, so a game never has to declare a
// class just to express "do this each tick".
// ============================================================================

class LambdaAction : public NPCAction {
public:
    using Tick  = std::function<ActionStatus(float, ActionContext&)>;
    using Start = std::function<void(ActionContext&)>;

    LambdaAction(std::string name, Tick tick, Start onStart = {})
        : m_tick(std::move(tick)), m_start(std::move(onStart)) {
        m_name = std::move(name);
    }

    void start(ActionContext& ctx) override { if (m_start) m_start(ctx); }
    ActionStatus update(float dt, ActionContext& ctx) override {
        return m_tick ? m_tick(dt, ctx) : ActionStatus::Failure;
    }

private:
    Tick  m_tick;
    Start m_start;
};

/// Convenience: wrap a tick lambda as an action.
inline std::shared_ptr<NPCAction> makeAction(std::string name,
                                             LambdaAction::Tick tick,
                                             LambdaAction::Start onStart = {}) {
    return std::make_shared<LambdaAction>(std::move(name), std::move(tick), std::move(onStart));
}

} // namespace AI
} // namespace Phyxel
