#pragma once
// Keep-clear boxes: world-space cube boxes that GENERATION must not fill with
// furniture or props. The first source is the game definition's scene-transition
// regions (a trapdoor region in a tavern storeroom): the generator has no idea a
// designer put a hatch there, and regen #16 of Ravenmere parked a barrel on the
// trapdoor cell (WalkabilityGateAndPlaytestLoop increment 7 follow-up). The
// designer's anchors are inputs to the generator, not things it may bury.
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Core {

struct KeepClearBox {
    glm::ivec3  min{0};   ///< inclusive cube corner
    glm::ivec3  max{0};   ///< inclusive cube corner
    std::string label;    ///< where it came from (trigger id) - for honest reporting
};

/// Pure: the keep-clear boxes for a TriggerSystem::regionTriggers() list. A region
/// is {from:{x,y,z}, to:{x,y,z}} in world units (floats allowed); the box covers
/// every cube the region touches. Malformed entries are skipped.
inline std::vector<KeepClearBox> keepClearFromRegionTriggers(
        const std::vector<std::pair<std::string, nlohmann::json>>& regions) {
    std::vector<KeepClearBox> out;
    for (const auto& [id, r] : regions) {
        if (!r.is_object() || !r.contains("from") || !r.contains("to")) continue;
        const auto& a = r["from"]; const auto& b = r["to"];
        if (!a.is_object() || !b.is_object()) continue;
        auto f = [](const nlohmann::json& j, const char* k) { return j.value(k, 0.0f); };
        KeepClearBox box;
        box.min = glm::ivec3(static_cast<int>(std::floor(std::min(f(a, "x"), f(b, "x")))),
                             static_cast<int>(std::floor(std::min(f(a, "y"), f(b, "y")))),
                             static_cast<int>(std::floor(std::min(f(a, "z"), f(b, "z")))));
        // `to` is an exclusive-feeling upper edge in authored regions ((-27..-26) means
        // the cube -27); a region whose `to` lands exactly on a cube boundary still
        // covers that cube, so ceil-1 would drop authored single-cube regions. Floor
        // both edges: an authored (x -27 .. -26) covers cubes -27 and -26 - one cube
        // of slack past the hatch is the right side to err on.
        box.max = glm::ivec3(static_cast<int>(std::floor(std::max(f(a, "x"), f(b, "x")))),
                             static_cast<int>(std::floor(std::max(f(a, "y"), f(b, "y")))),
                             static_cast<int>(std::floor(std::max(f(a, "z"), f(b, "z")))));
        box.label = id;
        out.push_back(std::move(box));
    }
    return out;
}

}  // namespace Core
}  // namespace Phyxel
