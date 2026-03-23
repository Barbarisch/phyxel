#include "graphics/DayNightCycle.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Graphics {

DayNightCycle::DayNightCycle()
    : m_timeOfDay(12.0f)       // Start at noon
    , m_dayLengthSeconds(600.0f) // 10-minute full day
    , m_timeScale(1.0f)
    , m_enabled(false)         // Off by default
    , m_paused(false)
    , m_sunDirection(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)))
    , m_sunColor(1.0f, 1.0f, 1.0f)
    , m_ambientStrength(1.0f) {
    recalculate();
}

void DayNightCycle::update(float deltaTime) {
    if (!m_enabled || m_paused || m_dayLengthSeconds <= 0.0f) return;

    float hoursPerSecond = 24.0f / m_dayLengthSeconds;
    m_timeOfDay += deltaTime * hoursPerSecond * m_timeScale;

    // Wrap around
    while (m_timeOfDay >= 24.0f) m_timeOfDay -= 24.0f;
    while (m_timeOfDay < 0.0f) m_timeOfDay += 24.0f;

    recalculate();
}

void DayNightCycle::setTimeOfDay(float hours) {
    m_timeOfDay = std::fmod(hours, 24.0f);
    if (m_timeOfDay < 0.0f) m_timeOfDay += 24.0f;
    recalculate();
}

void DayNightCycle::setDayLengthSeconds(float seconds) {
    m_dayLengthSeconds = std::max(1.0f, seconds);
}

void DayNightCycle::recalculate() {
    // Sun angle: 0h = midnight (below horizon), 6h = dawn, 12h = noon, 18h = dusk
    float hourAngle = (m_timeOfDay / 24.0f) * glm::two_pi<float>() - glm::half_pi<float>();
    // At noon (12h), hourAngle = pi/2 -> sun overhead
    // At midnight (0h), hourAngle = -pi/2 -> sun below

    float sunY = std::sin(hourAngle); // -1 at midnight, +1 at noon
    float sunXZ = std::cos(hourAngle);

    // Sun travels east to west (positive X at dawn, negative X at dusk)
    m_sunDirection = glm::normalize(glm::vec3(-sunXZ * 0.7f, -sunY, -sunXZ * 0.3f));

    // Sun elevation factor: 0 when below horizon, 1 at zenith
    float elevation = std::max(0.0f, sunY);

    // Ambient light: brighter during day, dim at night
    // Night minimum 0.06, day maximum 1.0
    float dayFactor = std::max(0.0f, sunY); // 0 at horizon, 1 at zenith
    float twilightFactor = std::clamp((sunY + 0.15f) / 0.3f, 0.0f, 1.0f); // smooth transition around horizon
    m_ambientStrength = glm::mix(0.06f, 1.0f, twilightFactor * std::sqrt(std::max(0.0f, twilightFactor)));

    // Sun color: white at noon, warm orange at dawn/dusk, off at night
    if (sunY <= -0.15f) {
        // Night — no sun
        m_sunColor = glm::vec3(0.0f);
    } else if (sunY < 0.2f) {
        // Dawn/dusk transition: warm orange
        float t = std::clamp((sunY + 0.15f) / 0.35f, 0.0f, 1.0f);
        glm::vec3 horizonColor(1.0f, 0.45f, 0.15f);
        glm::vec3 dayColor(1.0f, 0.98f, 0.92f);
        m_sunColor = glm::mix(glm::vec3(0.0f), horizonColor, t);
    } else {
        // Daytime: transition from warm to white
        float t = std::clamp((sunY - 0.2f) / 0.5f, 0.0f, 1.0f);
        glm::vec3 horizonColor(1.0f, 0.45f, 0.15f);
        glm::vec3 noonColor(1.0f, 0.98f, 0.92f);
        m_sunColor = glm::mix(horizonColor, noonColor, t);
    }
}

nlohmann::json DayNightCycle::toJson() const {
    return {
        {"timeOfDay", m_timeOfDay},
        {"dayLengthSeconds", m_dayLengthSeconds},
        {"timeScale", m_timeScale},
        {"enabled", m_enabled},
        {"paused", m_paused},
        {"sunDirection", {{"x", m_sunDirection.x}, {"y", m_sunDirection.y}, {"z", m_sunDirection.z}}},
        {"sunColor", {{"r", m_sunColor.r}, {"g", m_sunColor.g}, {"b", m_sunColor.b}}},
        {"ambientStrength", m_ambientStrength}
    };
}

void DayNightCycle::fromJson(const nlohmann::json& j) {
    if (j.contains("timeOfDay")) setTimeOfDay(j["timeOfDay"].get<float>());
    if (j.contains("dayLengthSeconds")) setDayLengthSeconds(j["dayLengthSeconds"].get<float>());
    if (j.contains("timeScale")) m_timeScale = j["timeScale"].get<float>();
    if (j.contains("enabled")) m_enabled = j["enabled"].get<bool>();
    if (j.contains("paused")) m_paused = j["paused"].get<bool>();
}

} // namespace Graphics
} // namespace Phyxel
