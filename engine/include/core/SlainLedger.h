#pragma once

#include <nlohmann/json.hpp>

#include <set>
#include <string>

namespace Phyxel {
namespace Core {

/// Which authored NPCs the player has killed, per scene, so a scene that is re-entered does not
/// re-spawn them (G-171: "Wolf enemies respawn when coming back to the hallow farm").
///
/// WHY A LEDGER AND NOT A STORY VARIABLE: the kill already sets `wolves_cleared` through a
/// trigger, and nothing consumed it - a scene load simply re-runs the scene's `npcs` list. A
/// per-NPC story variable would put the burden on every author for every enemy. Recording the
/// kill where it happens and consulting it where spawning happens makes "cleared stays cleared"
/// the default, and an NPC that SHOULD come back (wildlife, guards) opts out with
/// `"respawns": true` in its definition.
///
/// Keyed by scene AND entity id: NPC ids are `npc_<name>` and names are only unique within a
/// scene, so "npc_Wolf" on the farm must not shadow a "npc_Wolf" in the barrow. A single-scene
/// game passes an empty scene id and everything lands in one bucket.
///
/// Lives in the PlayerProfile so it rides the existing save path; the engine's loader reads it
/// through GameSubsystems::isNpcSlain rather than depending on the profile type.
struct SlainLedger {
    std::set<std::string> keys;

    static std::string key(const std::string& sceneId, const std::string& entityId) {
        return sceneId + ":" + entityId;
    }
    void record(const std::string& sceneId, const std::string& entityId) {
        if (!entityId.empty()) keys.insert(key(sceneId, entityId));
    }
    bool contains(const std::string& sceneId, const std::string& entityId) const {
        return keys.count(key(sceneId, entityId)) != 0;
    }
    /// A long rest, a "respawn everything" debug command, or a new game.
    void clear() { keys.clear(); }
    size_t size() const { return keys.size(); }

    nlohmann::json toJson() const {
        nlohmann::json j = nlohmann::json::array();
        for (const auto& k : keys) j.push_back(k);
        return j;
    }
    void fromJson(const nlohmann::json& j) {
        keys.clear();
        if (!j.is_array()) return;
        for (const auto& v : j) if (v.is_string()) keys.insert(v.get<std::string>());
    }
};

}  // namespace Core
}  // namespace Phyxel
