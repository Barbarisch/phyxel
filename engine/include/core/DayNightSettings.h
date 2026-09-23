#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <utility>

namespace Phyxel {
namespace Core {

/// The authored day/night block of a game definition (G-165):
///
///   "dayNight": { "enabled": true, "timeOfDay": 9.0, "dayLengthSeconds": 1200,
///                 "timeScale": 1.0, "paused": false, "dayNumber": 1 }
///
/// Every field is optional; an absent field leaves the cycle's current value alone, so a game
/// can say only `"enabled": true` and keep the engine defaults. Field names match the existing
/// /api/daynight/set route and the MCP `set_day_night` tool exactly, so a value works the same
/// whether it is authored, scripted or poked at runtime.
///
/// Lives in core, not graphics, for the same layering reason the "sky" block is passed through
/// unparsed: engine/core must not depend on graphics. Graphics::DayNightCycle::applySettings()
/// consumes it.
///
/// WHY THIS EXISTS: DayNightCycle is OFF by default and, before this block, nothing in a shipped
/// game could turn it on - only the editor's ImGui panel and its HTTP route could. A shipped
/// game therefore sat at a fixed noon forever (Ravenmere feedback 2026-09-23: "i dont see
/// day/night cycle going").
struct DayNightSettings {
    std::optional<bool>  enabled;
    std::optional<float> timeOfDay;          ///< hours, 0..24 (24 wraps to 0)
    std::optional<float> dayLengthSeconds;   ///< real seconds per full day; must be > 0
    std::optional<float> timeScale;          ///< speed multiplier; must be >= 0
    std::optional<bool>  paused;
    std::optional<int>   dayNumber;          ///< must be >= 1

    /// True when at least one field is set.
    bool any() const {
        return enabled || timeOfDay || dayLengthSeconds || timeScale || paused || dayNumber;
    }

    /// Parse the block. Unknown keys are ignored; wrong-typed or out-of-range values are
    /// reported by validate(), which the loader's validate() calls - parsing itself is lenient.
    static DayNightSettings fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;

    /// {ok, error}. The same rules the loader enforces, so a bad block is refused with a
    /// message naming the field instead of silently clamped into something else.
    static std::pair<bool, std::string> validate(const nlohmann::json& j);
};

}  // namespace Core
}  // namespace Phyxel
