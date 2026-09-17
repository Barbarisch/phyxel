#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Phyxel {
namespace Core {

/// One NPC as the initiator sees it.
struct AggroCandidate {
    std::string id;
    glm::vec3   pos{0.0f};
    bool        hostile = false;
    bool        alive   = true;
};

/// How turn-based encounters START other than from an authored trigger region
/// (Ravenmere G-137: "walk up to the alpha wolf but get nothing, cannot interact";
/// "combat needs to be a little more sophisticated"; "cast spells outside of combat
/// which would then trigger combat"). Three rules, all pure and testable:
///
///  * the player engages a hostile (a click on it, an opening spell) — the hostiles
///    near that one JOIN the fight: gatherGroup;
///  * a hostile that NOTICES the player (within range, line of sight clear) engages
///    on its own: notices;
///  * an escapee (morale flee, CombatAISystem) is beaten, not brave — it does not
///    re-engage until a cooldown lapses, but a fresh attack on it is always a fight:
///    noteFled / mayEngage.
///
/// The host (the shipped shell) owns the entities, the encounter and the voxel
/// line-of-sight test; this class owns the rules and the cooldown book.
class EncounterInitiator {
public:
    static constexpr float kGroupRadius     = 12.0f;  ///< hostiles this close to the engaged one join (world units)
    static constexpr float kSightRange      = 8.0f;   ///< a hostile that sees the player this close engages
    static constexpr float kEyeHeight       = 1.5f;   ///< line of sight runs eye to eye
    static constexpr float kFledCooldownSec = 30.0f;  ///< an escapee keeps its distance this long

    /// Hostile, living candidates within `radius` (XZ) of the anchor, the anchor first
    /// (it is always included when it is a hostile, living candidate itself).
    static std::vector<std::string> gatherGroup(const std::string& anchorId, const glm::vec3& anchorPos,
                                                float radius, const std::vector<AggroCandidate>& cands) {
        std::vector<std::string> out;
        const AggroCandidate* anchor = nullptr;
        for (const auto& c : cands) if (c.id == anchorId) { anchor = &c; break; }
        if (anchor && anchor->hostile && anchor->alive) out.push_back(anchorId);
        for (const auto& c : cands) {
            if (c.id == anchorId || !c.hostile || !c.alive) continue;
            const float dx = c.pos.x - anchorPos.x, dz = c.pos.z - anchorPos.z;
            if (std::sqrt(dx * dx + dz * dz) <= radius) out.push_back(c.id);
        }
        return out;
    }

    /// Does an NPC at `npcPos` notice a player at `playerPos`: within `range` (3D) and
    /// nothing solid between their eyes. `blocked(a, b)` answers whether the segment
    /// a->b crosses a solid voxel (the host marches the world); a null test = open field.
    static bool notices(const glm::vec3& npcPos, const glm::vec3& playerPos, float range,
                        const std::function<bool(const glm::vec3&, const glm::vec3&)>& blocked) {
        const glm::vec3 d = playerPos - npcPos;
        if (glm::dot(d, d) > range * range) return false;
        if (!blocked) return true;
        const glm::vec3 up(0.0f, kEyeHeight, 0.0f);
        return !blocked(npcPos + up, playerPos + up);
    }

    void noteFled(const std::string& id, double now) { m_fledUntil[id] = now + kFledCooldownSec; }
    bool mayEngage(const std::string& id, double now) const {
        auto it = m_fledUntil.find(id);
        return it == m_fledUntil.end() || now >= it->second;
    }
    bool hasFled(const std::string& id) const { return m_fledUntil.count(id) != 0; }
    void clearFled() { m_fledUntil.clear(); }

private:
    std::unordered_map<std::string, double> m_fledUntil;
};

} // namespace Core
} // namespace Phyxel
