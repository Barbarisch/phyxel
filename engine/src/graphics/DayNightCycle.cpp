#include "graphics/DayNightCycle.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Graphics {

DayNightCycle::DayNightCycle()
    : m_timeOfDay(12.0f)       // Start at noon
    , m_dayNumber(1)           // Day 1
    , m_dayLengthSeconds(600.0f) // 10-minute full day
    , m_timeScale(1.0f)
    , m_enabled(false)         // Off by default
    , m_paused(false)
    , m_sunDirection(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)))
    , m_sunColor(1.0f, 1.0f, 1.0f)
    , m_ambientStrength(1.0f)
    , m_skyColor(0.45f, 0.65f, 0.95f) {
    recalculate();
}

void DayNightCycle::update(float deltaTime) {
    if (!m_enabled || m_paused || m_dayLengthSeconds <= 0.0f) return;

    float hoursPerSecond = 24.0f / m_dayLengthSeconds;
    m_timeOfDay += deltaTime * hoursPerSecond * m_timeScale;

    // Wrap around and increment day counter
    while (m_timeOfDay >= 24.0f) {
        m_timeOfDay -= 24.0f;
        m_dayNumber++;
    }
    while (m_timeOfDay < 0.0f) {
        m_timeOfDay += 24.0f;
        m_dayNumber = std::max(1, m_dayNumber - 1);
    }

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

float DayNightCycle::getMoonIlluminatedFraction() const {
    // Same closed form as Atmosphere::moonIlluminatedFraction, and it must stay the same: the light
    // the moon casts and the lit area drawn on its disc have to agree.
    return 0.5f * (1.0f - std::cos(glm::two_pi<float>() * m_moonPhase01));
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

    // ---- Moon: same swing plane, LAGGING the sun by the phase angle -----------------------------
    // Phase comes from the day number over WorldClock's 28-day lunar cycle, and the moon's position
    // is the sun's hour angle minus 2*pi*phase. That one subtraction gives correct behaviour for
    // free: at phase 0 (new) the moon sits with the sun and is invisible; at phase 0.5 (full) it is
    // half a cycle behind, i.e. 180 degrees away, so it rises exactly as the sun sets. Nothing
    // scripts that — it is where the geometry puts it. The renderer's disc shading then derives the
    // terminator from this direction versus the sun's, so the drawn phase always matches the orbit.
    constexpr int kLunarCycleDays = 28;   // matches Core::LUNAR_CYCLE_DAYS (WorldClock.h)
    {
        const int dayInCycle = ((m_dayNumber % kLunarCycleDays) + kLunarCycleDays) % kLunarCycleDays;
        m_moonPhase01 = static_cast<float>(dayInCycle) / static_cast<float>(kLunarCycleDays);
        const float moonHourAngle = hourAngle - glm::two_pi<float>() * m_moonPhase01;
        const float moonY  = std::sin(moonHourAngle);
        const float moonXZ = std::cos(moonHourAngle);
        m_moonDirection = glm::normalize(glm::vec3(-moonXZ * 0.7f, -moonY, -moonXZ * 0.3f));
    }

    // Sun elevation factor: 0 when below horizon, 1 at zenith
    float elevation = std::max(0.0f, sunY);

    // Ambient light: brighter during day, dim at night
    // Night minimum 0.06, day maximum 1.0
    float dayFactor = std::max(0.0f, sunY); // 0 at horizon, 1 at zenith
    float twilightFactor = std::clamp((sunY + 0.15f) / 0.3f, 0.0f, 1.0f); // smooth transition around horizon
    m_ambientStrength = glm::mix(0.06f, 1.0f, twilightFactor * std::sqrt(std::max(0.0f, twilightFactor)));

    // Sun color: white at noon, warm orange at dawn/dusk, off at night
    const glm::vec3 horizonColor(1.0f, 0.42f, 0.14f); // warm sunrise/sunset
    const glm::vec3 noonColor(1.0f, 0.97f, 0.90f);    // near-white midday
    if (sunY <= -0.15f) {
        // Night — no sun
        m_sunColor = glm::vec3(0.0f);
    } else if (sunY < 0.2f) {
        // Dawn/dusk transition: fade in the warm horizon colour as the sun rises past the horizon
        float t = std::clamp((sunY + 0.15f) / 0.35f, 0.0f, 1.0f);
        m_sunColor = glm::mix(glm::vec3(0.0f), horizonColor, t);
    } else {
        // Daytime: transition from warm horizon to near-white noon
        float t = std::clamp((sunY - 0.2f) / 0.5f, 0.0f, 1.0f);
        m_sunColor = glm::mix(horizonColor, noonColor, t);
    }

    // Sky/background colour by sun elevation: deep night blue → warm horizon at dawn/dusk →
    // clear day blue. Dawn and dusk share the same elevation so they look alike (fine).
    const glm::vec3 nightSky(0.015f, 0.025f, 0.06f); // deep blue, not pure black
    const glm::vec3 horizonSky(0.80f, 0.50f, 0.38f); // warm dawn/dusk glow
    const glm::vec3 daySky(0.45f, 0.65f, 0.95f);     // clear midday blue
    if (sunY <= -0.18f) {
        m_skyColor = nightSky;
    } else if (sunY < 0.12f) {
        float t = std::clamp((sunY + 0.18f) / 0.30f, 0.0f, 1.0f); // night → horizon
        m_skyColor = glm::mix(nightSky, horizonSky, t);
    } else {
        float t = std::clamp((sunY - 0.12f) / 0.40f, 0.0f, 1.0f); // horizon → day
        m_skyColor = glm::mix(horizonSky, daySky, t);
    }
}

nlohmann::json DayNightCycle::toJson() const {
    return {
        {"timeOfDay", m_timeOfDay},
        {"hour", getHour()},
        {"minute", getMinute()},
        {"dayNumber", m_dayNumber},
        {"isNight", isNight()},
        {"dayLengthSeconds", m_dayLengthSeconds},
        {"timeScale", m_timeScale},
        {"enabled", m_enabled},
        {"paused", m_paused},
        {"sunDirection", {{"x", m_sunDirection.x}, {"y", m_sunDirection.y}, {"z", m_sunDirection.z}}},
        {"sunColor", {{"r", m_sunColor.r}, {"g", m_sunColor.g}, {"b", m_sunColor.b}}},
        {"skyColor", {{"r", m_skyColor.r}, {"g", m_skyColor.g}, {"b", m_skyColor.b}}},
        {"ambientStrength", m_ambientStrength}
    };
}

void DayNightCycle::fromJson(const nlohmann::json& j) {
    if (j.contains("timeOfDay")) setTimeOfDay(j["timeOfDay"].get<float>());
    if (j.contains("dayNumber")) m_dayNumber = j["dayNumber"].get<int>();
    if (j.contains("dayLengthSeconds")) setDayLengthSeconds(j["dayLengthSeconds"].get<float>());
    if (j.contains("timeScale")) m_timeScale = j["timeScale"].get<float>();
    if (j.contains("enabled")) m_enabled = j["enabled"].get<bool>();
    if (j.contains("paused")) m_paused = j["paused"].get<bool>();
}

} // namespace Graphics
} // namespace Phyxel
