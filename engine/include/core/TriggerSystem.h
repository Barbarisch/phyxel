#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Phyxel {
namespace Core {

// ============================================================================
// TriggerSystem — declarative when/then gameplay triggers.
//
// Lets a game definition (game.json) or an agent (MCP) express win conditions
// and game-flow rules as data instead of hand-written C++:
//
//   { "id": "win",
//     "when": { "event": "player_jumped" },
//     "then": [ { "type": "transition_scene", "target": "credits" } ],
//     "once": true }
//
// Conditions ("when"):
//   - "event": any gameplay event name fed through onEvent() — e.g.
//     player_jumped, player_landed, objective_complete. If "when" also carries
//     an "id", the event payload's "id" must match (objective_complete {id}).
//   - "event": "timer" — fires once "seconds" have elapsed since the trigger
//     was added.
//   - "event": "entity_reached_region" — fires when "entity" ("player" or an
//     entity id) ENTERS the AABB "region" {from{x,y,z}, to{x,y,z}}.
//
// Actions ("then": array): executed through the host-provided ActionExecutor,
// keeping this system decoupled from the engine front-end. Hosts implement at
// least: complete_objective {id}, fail_objective {id},
// transition_scene {target}, quit_game.
//
// Fired actions are QUEUED and drained inside update(), so they always execute
// at the host's safe point in the frame, never mid-event-dispatch.
// ============================================================================

class TriggerSystem {
public:
    /// Executes one action ("then" entry). triggerId identifies the source trigger.
    using ActionExecutor = std::function<void(const nlohmann::json& action,
                                              const std::string& triggerId)>;
    /// Resolve an entity id ("player" included) to a world position.
    /// Returns false if the entity is unknown.
    using PositionResolver = std::function<bool(const std::string& entityId, glm::vec3& outPos)>;
    /// Optional sink for the system's own notifications (e.g. "trigger_fired").
    using EventSink = std::function<void(const std::string& type, const nlohmann::json& data)>;

    void setActionExecutor(ActionExecutor fn) { m_execute = std::move(fn); }
    void setEventSink(EventSink fn) { m_eventSink = std::move(fn); }

    /// Feed a gameplay event. Matching triggers are marked fired and their
    /// actions queued (executed on the next update()).
    void onEvent(const std::string& type, const nlohmann::json& data = nlohmann::json::object());

    /// Per-frame: advance timers, evaluate region conditions, then drain the
    /// queued actions through the ActionExecutor.
    void update(float dt, const PositionResolver& resolvePos = {});

    /// Add a trigger from its JSON definition. Returns the trigger id ("" on
    /// error, with *error describing why). Auto-assigns an id when absent.
    std::string addTrigger(const nlohmann::json& def, std::string* error = nullptr);

    bool removeTrigger(const std::string& id);
    void clear();
    nlohmann::json listTriggers() const;
    size_t count() const { return m_triggers.size(); }

    /// Load a game definition's "triggers" array. Returns how many were added.
    int loadFromJson(const nlohmann::json& triggersArray);

    /// An active timer trigger that asked for an on-screen countdown.
    /// Authored as: { "when": {"event":"timer","seconds":60}, "hud": true,
    ///                "hudLabel": "Escape!" , ... }
    struct CountdownInfo {
        std::string id;
        std::string label;       // optional "hudLabel" text shown before the time
        float remaining = 0.0f;  // seconds left (clamped >= 0)
        float total     = 0.0f;  // authored when.seconds
    };

    /// Countdowns to display this frame (timer triggers with "hud": true that
    /// haven't fired yet). Hosts render these — see UI::renderCountdownHud.
    std::vector<CountdownInfo> getActiveCountdowns() const;

private:
    struct Trigger {
        std::string id;
        std::string event;       // when.event
        nlohmann::json when;     // full "when" block (id / seconds / entity / region)
        nlohmann::json actions;  // "then" array
        bool  once = true;
        bool  fired = false;     // suppresses re-fire when once
        float timerElapsed = 0.0f;
        bool  wasInside = false; // entity_reached_region enter-edge detection
        bool  hud = false;       // show an on-screen countdown (timer triggers)
        std::string hudLabel;    // optional label next to the countdown
    };

    void fire(Trigger& t);
    static bool regionContains(const nlohmann::json& region, const glm::vec3& p);

    std::vector<Trigger> m_triggers;
    std::vector<std::pair<nlohmann::json, std::string>> m_pendingActions; // (action, triggerId)
    ActionExecutor m_execute;
    EventSink      m_eventSink;
    int m_autoId = 0;
};

} // namespace Core
} // namespace Phyxel
