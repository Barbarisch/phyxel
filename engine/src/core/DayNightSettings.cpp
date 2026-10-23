#include "core/DayNightSettings.h"

namespace Phyxel {
namespace Core {

using json = nlohmann::json;

DayNightSettings DayNightSettings::fromJson(const json& j) {
    DayNightSettings s;
    if (!j.is_object()) return s;
    if (j.contains("enabled")          && j["enabled"].is_boolean())          s.enabled          = j["enabled"].get<bool>();
    if (j.contains("timeOfDay")        && j["timeOfDay"].is_number())         s.timeOfDay        = j["timeOfDay"].get<float>();
    if (j.contains("dayLengthSeconds") && j["dayLengthSeconds"].is_number())  s.dayLengthSeconds = j["dayLengthSeconds"].get<float>();
    if (j.contains("timeScale")        && j["timeScale"].is_number())         s.timeScale        = j["timeScale"].get<float>();
    if (j.contains("paused")           && j["paused"].is_boolean())           s.paused           = j["paused"].get<bool>();
    if (j.contains("dayNumber")        && j["dayNumber"].is_number_integer()) s.dayNumber        = j["dayNumber"].get<int>();
    return s;
}

json DayNightSettings::toJson() const {
    json j = json::object();
    if (enabled)          j["enabled"]          = *enabled;
    if (timeOfDay)        j["timeOfDay"]        = *timeOfDay;
    if (dayLengthSeconds) j["dayLengthSeconds"] = *dayLengthSeconds;
    if (timeScale)        j["timeScale"]        = *timeScale;
    if (paused)           j["paused"]           = *paused;
    if (dayNumber)        j["dayNumber"]        = *dayNumber;
    return j;
}

std::pair<bool, std::string> DayNightSettings::validate(const json& j) {
    if (!j.is_object()) return {false, "'dayNight' must be an object"};

    auto numberField = [&](const char* key, float lo, float hi, bool loInclusive,
                           std::string& err) -> bool {
        if (!j.contains(key)) return true;
        if (!j[key].is_number()) { err = std::string("'dayNight.") + key + "' must be a number"; return false; }
        const float v = j[key].get<float>();
        const bool ok = (loInclusive ? v >= lo : v > lo) && v <= hi;
        if (!ok) {
            err = std::string("'dayNight.") + key + "' out of range: " + std::to_string(v);
            return false;
        }
        return true;
    };

    std::string err;
    // Hours on a 24-hour clock. 24 itself is accepted (it wraps to 0); 25 is a typo, not a time.
    if (!numberField("timeOfDay", 0.0f, 24.0f, true, err)) return {false, err};
    // A day of zero or negative seconds would divide by zero in update(); the cycle clamps to
    // 1 s but that hides an authoring mistake as a strobing sky.
    if (!numberField("dayLengthSeconds", 0.0f, 1e9f, false, err)) return {false, err};
    if (!numberField("timeScale", 0.0f, 1e6f, true, err)) return {false, err};

    for (const char* key : {"enabled", "paused"})
        if (j.contains(key) && !j[key].is_boolean())
            return {false, std::string("'dayNight.") + key + "' must be true or false"};

    if (j.contains("dayNumber")) {
        if (!j["dayNumber"].is_number_integer()) return {false, "'dayNight.dayNumber' must be an integer"};
        if (j["dayNumber"].get<int>() < 1) return {false, "'dayNight.dayNumber' must be >= 1"};
    }
    return {true, ""};
}

}  // namespace Core
}  // namespace Phyxel
