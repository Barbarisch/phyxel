#pragma once
// Transition sites are VISIBLE (WalkabilityGateAndPlaytestLoop increment 7). Every
// `entity_reached_region` trigger that transitions scenes gets a prop placed at its
// region when the definition loads: a trapdoor for a hatch, the ladder for a stair
// well, a waystone beside a road exit - so the player can see where the world
// continues, without a quest marker. Authoring: `when.marker` (template stem, default
// "waystone", "none" to opt out), `when.marker_offset` {x,z} cubes (default: a
// waystone stands beside the region, perpendicular to its long axis; a trapdoor or
// ladder sits at its centre), `when.interact` (fire on the interact key inside the
// region, not on entry).
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

struct TransitionMarker {
    std::string triggerId;
    std::string templateName;
    glm::ivec3  position{0};   ///< cube position for the placed object (min corner)
    bool        interact = false;
};

/// Pure: plan the markers for a `triggers` array. Deterministic, no placement.
std::vector<TransitionMarker> planTransitionMarkers(const nlohmann::json& triggers);

/// Templates that sit ON the region (flush / climbable) rather than beside it.
bool isOnSiteMarker(const std::string& templateName);

}  // namespace Core
}  // namespace Phyxel
