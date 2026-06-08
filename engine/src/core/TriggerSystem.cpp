#include "core/TriggerSystem.h"
#include "utils/Logger.h"

#include <algorithm>

namespace Phyxel {
namespace Core {

using nlohmann::json;

void TriggerSystem::onEvent(const std::string& type, const json& data) {
    for (Trigger& t : m_triggers) {
        if (t.fired && t.once) continue;
        if (t.event != type) continue;
        // Optional payload-id matching (e.g. objective_complete {id: "win"}).
        if (t.when.contains("id")) {
            if (!data.contains("id") || data["id"] != t.when["id"]) continue;
        }
        fire(t);
    }
}

void TriggerSystem::update(float dt, const PositionResolver& resolvePos) {
    for (Trigger& t : m_triggers) {
        if (t.fired && t.once) continue;

        if (t.event == "timer") {
            t.timerElapsed += dt;
            const float seconds = t.when.value("seconds", 0.0f);
            if (seconds > 0.0f && t.timerElapsed >= seconds) {
                fire(t);
                t.timerElapsed = 0.0f; // repeating timers (once=false) restart
            }
        } else if (t.event == "entity_reached_region") {
            if (!resolvePos) continue;
            const std::string entity = t.when.value("entity", "player");
            glm::vec3 pos;
            if (!resolvePos(entity, pos)) { t.wasInside = false; continue; }
            const bool inside = t.when.contains("region") && regionContains(t.when["region"], pos);
            if (inside && !t.wasInside) fire(t);
            t.wasInside = inside;
        }
    }

    // Drain queued actions at this (host-chosen) safe point.
    if (!m_pendingActions.empty()) {
        auto pending = std::move(m_pendingActions);
        m_pendingActions.clear();
        for (const auto& [action, triggerId] : pending) {
            if (m_execute) {
                m_execute(action, triggerId);
            } else {
                LOG_WARN("TriggerSystem", "Trigger '{}' fired but no ActionExecutor is set", triggerId);
            }
        }
    }
}

void TriggerSystem::fire(Trigger& t) {
    t.fired = true;
    if (t.actions.is_array()) {
        for (const auto& action : t.actions) {
            m_pendingActions.emplace_back(action, t.id);
        }
    }
    if (m_eventSink) {
        m_eventSink("trigger_fired", {{"id", t.id}, {"event", t.event}});
    }
    LOG_INFO("TriggerSystem", "Trigger '{}' fired (when: {})", t.id, t.event);
}

bool TriggerSystem::regionContains(const json& region, const glm::vec3& p) {
    if (!region.contains("from") || !region.contains("to")) return false;
    const json& a = region["from"];
    const json& b = region["to"];
    const float x0 = std::min(a.value("x", 0.0f), b.value("x", 0.0f));
    const float x1 = std::max(a.value("x", 0.0f), b.value("x", 0.0f));
    const float y0 = std::min(a.value("y", 0.0f), b.value("y", 0.0f));
    const float y1 = std::max(a.value("y", 0.0f), b.value("y", 0.0f));
    const float z0 = std::min(a.value("z", 0.0f), b.value("z", 0.0f));
    const float z1 = std::max(a.value("z", 0.0f), b.value("z", 0.0f));
    return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1 && p.z >= z0 && p.z <= z1;
}

std::string TriggerSystem::addTrigger(const json& def, std::string* error) {
    auto failWith = [&](const std::string& msg) -> std::string {
        if (error) *error = msg;
        return "";
    };
    if (!def.is_object())            return failWith("trigger must be an object");
    if (!def.contains("when") || !def["when"].is_object())
        return failWith("trigger needs a 'when' object");
    const json& when = def["when"];
    const std::string event = when.value("event", "");
    if (event.empty())               return failWith("'when.event' is required");
    if (!def.contains("then") || !def["then"].is_array() || def["then"].empty())
        return failWith("trigger needs a non-empty 'then' array");
    if (event == "timer" && when.value("seconds", 0.0f) <= 0.0f)
        return failWith("timer trigger needs 'when.seconds' > 0");
    if (event == "entity_reached_region" &&
        (!when.contains("region") || !when["region"].is_object()))
        return failWith("entity_reached_region trigger needs 'when.region' {from,to}");

    Trigger t;
    t.id = def.value("id", "");
    if (t.id.empty()) t.id = "trigger_" + std::to_string(++m_autoId);
    // Replace an existing trigger with the same id (idempotent authoring).
    removeTrigger(t.id);
    t.event    = event;
    t.when     = when;
    t.actions  = def["then"];
    t.once     = def.value("once", true);
    t.hud      = def.value("hud", false);
    t.hudLabel = def.value("hudLabel", "");
    m_triggers.push_back(std::move(t));
    return m_triggers.back().id;
}

std::vector<TriggerSystem::CountdownInfo> TriggerSystem::getActiveCountdowns() const {
    std::vector<CountdownInfo> out;
    for (const Trigger& t : m_triggers) {
        if (!t.hud || t.event != "timer") continue;
        if (t.fired && t.once) continue;
        const float total = t.when.value("seconds", 0.0f);
        if (total <= 0.0f) continue;
        out.push_back({t.id, t.hudLabel, std::max(0.0f, total - t.timerElapsed), total});
    }
    return out;
}

bool TriggerSystem::removeTrigger(const std::string& id) {
    auto it = std::remove_if(m_triggers.begin(), m_triggers.end(),
                             [&](const Trigger& t) { return t.id == id; });
    if (it == m_triggers.end()) return false;
    m_triggers.erase(it, m_triggers.end());
    return true;
}

void TriggerSystem::clear() {
    m_triggers.clear();
    m_pendingActions.clear();
}

json TriggerSystem::listTriggers() const {
    json arr = json::array();
    for (const Trigger& t : m_triggers) {
        json entry = {
            {"id", t.id},
            {"when", t.when},
            {"then", t.actions},
            {"once", t.once},
            {"fired", t.fired}
        };
        if (t.hud) {
            entry["hud"] = true;
            if (!t.hudLabel.empty()) entry["hudLabel"] = t.hudLabel;
            if (t.event == "timer")
                entry["remaining"] = std::max(0.0f, t.when.value("seconds", 0.0f) - t.timerElapsed);
        }
        arr.push_back(entry);
    }
    return arr;
}

int TriggerSystem::loadFromJson(const json& triggersArray) {
    if (!triggersArray.is_array()) return 0;
    int added = 0;
    for (const auto& def : triggersArray) {
        std::string err;
        if (!addTrigger(def, &err).empty()) {
            ++added;
        } else {
            LOG_WARN("TriggerSystem", "Skipping invalid trigger: {}", err);
        }
    }
    return added;
}

} // namespace Core
} // namespace Phyxel
