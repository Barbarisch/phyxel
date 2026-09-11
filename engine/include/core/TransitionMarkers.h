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

/// A placed object's cube box as the loader sees it (named apart from the validators' PlacedBox:
/// a same-named struct with another layout in one namespace is an ODR crash, found 2026-09-10).
struct MarkerSiteBox {
    std::string id;
    std::string templateName;
    glm::ivec3  min{0};
    glm::ivec3  max{0};
};

/// Idempotence: the marker is ALREADY in the world when a placed object of the same
/// template stands within one cube of its planned column (any y within two cubes -
/// a seated prop sits on the real surface, not at the authored region floor).
/// Regen #16 of Ravenmere: every scene load placed the props again (two trapdoors,
/// a waystone stacked on a waystone).
const MarkerSiteBox* markerAlreadyPlaced(const std::vector<MarkerSiteBox>& placed,
                                     const TransitionMarker& m);

/// Honest reporting: ids of OTHER placed objects whose box covers the marker's cell
/// (x/z inside, y within [pos.y - 1, pos.y + 2]) - furniture standing on a hatch.
std::vector<std::string> markerObstructions(const std::vector<MarkerSiteBox>& placed,
                                            const TransitionMarker& m);

}  // namespace Core
}  // namespace Phyxel
