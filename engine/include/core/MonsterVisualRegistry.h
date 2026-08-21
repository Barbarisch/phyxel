#pragma once

// Monster visual bindings: monsterId -> rig + animation mapping + faction.
//
// MonsterDefinition stays a pure D&D stat block (no appearance fields, so the
// SRD-ingested data never carries engine concerns). The link from a stat
// block to what it LOOKS like lives in one sidecar file,
// resources/monsters/visuals/bindings.json, loaded here. The subdirectory is
// deliberately outside MonsterRegistry::loadFromDirectory's non-recursive
// glob (the same dodge resources/rpg_items/anim/ uses).
//
// Consumed by the spawn_encounter handler (Application.cpp): stat block from
// MonsterRegistry + visual from here => a hostile NPC with the right rig.

#include <map>
#include <string>

#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

struct MonsterVisual {
    std::string monsterId;
    std::string animFile;
    std::map<std::string, std::string> animationMapping;  // FSM state -> clip
    nlohmann::json appearance;  // optional CharacterAppearance JSON block
    std::string faction = "monsters";
};

class MonsterVisualRegistry {
public:
    static MonsterVisualRegistry& instance();

    /// Load bindings.json once (idempotent). Returns bindings loaded.
    int ensureLoaded();

    /// nullptr when the monster has no visual binding.
    const MonsterVisual* get(const std::string& monsterId) const;

    size_t count() const { return m_visuals.size(); }
    void clear() { m_visuals.clear(); m_loaded = false; }

private:
    MonsterVisualRegistry() = default;
    std::map<std::string, MonsterVisual> m_visuals;
    bool m_loaded = false;
};

}  // namespace Core
}  // namespace Phyxel
