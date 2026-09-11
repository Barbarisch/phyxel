#include "core/TransitionMarkers.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Phyxel {
namespace Core {

bool isOnSiteMarker(const std::string& t) {
    return t == "trapdoor" || t == "ladder" || t == "hatch";
}

std::vector<TransitionMarker> planTransitionMarkers(const nlohmann::json& triggers) {
    std::vector<TransitionMarker> out;
    if (!triggers.is_array()) return out;
    for (const auto& t : triggers) {
        if (!t.is_object()) continue;
        const auto& when = t.value("when", nlohmann::json::object());
        if (when.value("event", std::string()) != "entity_reached_region") continue;
        if (!when.contains("region")) continue;
        bool transitions = false;
        if (t.contains("then") && t["then"].is_array())
            for (const auto& a : t["then"])
                if (a.is_object() && a.value("type", std::string()) == "transition_scene") transitions = true;
        if (!transitions) continue;
        const std::string tmpl = when.value("marker", std::string("waystone"));
        if (tmpl == "none") continue;

        const auto& a = when["region"]["from"];
        const auto& b = when["region"]["to"];
        const float x0 = std::min(a.value("x", 0.0f), b.value("x", 0.0f)), x1 = std::max(a.value("x", 0.0f), b.value("x", 0.0f));
        const float z0 = std::min(a.value("z", 0.0f), b.value("z", 0.0f)), z1 = std::max(a.value("z", 0.0f), b.value("z", 0.0f));
        const float y0 = std::min(a.value("y", 0.0f), b.value("y", 0.0f));
        float cx = (x0 + x1) * 0.5f, cz = (z0 + z1) * 0.5f;
        if (when.contains("marker_offset")) {
            cx += when["marker_offset"].value("x", 0.0f);
            cz += when["marker_offset"].value("z", 0.0f);
        } else if (!isOnSiteMarker(tmpl)) {
            // Beside the region, perpendicular to its long axis, one cube clear of it:
            // a road exit's waystone stands at the verge, not on the road.
            const float ex = x1 - x0, ez = z1 - z0;
            if (ex >= ez) cz = z1 + 1.0f; else cx = x1 + 1.0f;
        }
        TransitionMarker m;
        m.triggerId = t.value("id", std::string());
        m.templateName = tmpl;
        m.position = glm::ivec3(static_cast<int>(std::floor(cx)), static_cast<int>(std::floor(y0)),
                                static_cast<int>(std::floor(cz)));
        m.interact = when.value("interact", false);
        out.push_back(m);
    }
    return out;
}

}  // namespace Core
}  // namespace Phyxel

// ---------------------------------------------------------------------------
// Loader-side idempotence + obstruction reporting (pure over MarkerSiteBox lists).
// ---------------------------------------------------------------------------
namespace Phyxel {
namespace Core {

const MarkerSiteBox* markerAlreadyPlaced(const std::vector<MarkerSiteBox>& placed,
                                     const TransitionMarker& m) {
    for (const auto& p : placed) {
        if (p.templateName != m.templateName) continue;
        if (std::abs(p.min.x - m.position.x) > 1 || std::abs(p.min.z - m.position.z) > 1) continue;
        if (p.min.y < m.position.y - 2 || p.min.y > m.position.y + 2) continue;
        return &p;
    }
    return nullptr;
}

std::vector<std::string> markerObstructions(const std::vector<MarkerSiteBox>& placed,
                                            const TransitionMarker& m) {
    std::vector<std::string> out;
    for (const auto& p : placed) {
        if (p.templateName == m.templateName) continue;
        if (m.position.x < p.min.x || m.position.x > p.max.x) continue;
        if (m.position.z < p.min.z || m.position.z > p.max.z) continue;
        if (p.max.y < m.position.y - 1 || p.min.y > m.position.y + 2) continue;
        out.push_back(p.id);
    }
    return out;
}

}  // namespace Core
}  // namespace Phyxel
