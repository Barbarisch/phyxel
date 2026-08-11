#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

namespace Phyxel {
namespace Graphics {

/// Animates sun direction, sun color, and ambient light over a configurable day cycle.
/// Time is measured in hours (0.0–24.0), wrapping at 24.
class DayNightCycle {
public:
    DayNightCycle();

    /// Advance time by deltaTime seconds. Only advances when enabled.
    void update(float deltaTime);

    // Time control
    float getTimeOfDay() const { return m_timeOfDay; }
    void setTimeOfDay(float hours);
    void setDayLengthSeconds(float seconds);
    float getDayLengthSeconds() const { return m_dayLengthSeconds; }
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }
    void setPaused(bool paused) { m_paused = paused; }
    bool isPaused() const { return m_paused; }
    void setTimeScale(float scale) { m_timeScale = scale; }
    float getTimeScale() const { return m_timeScale; }

    // World time helpers
    int getHour() const { return static_cast<int>(m_timeOfDay); }
    int getMinute() const { return static_cast<int>((m_timeOfDay - static_cast<int>(m_timeOfDay)) * 60.0f); }
    int getDayNumber() const { return m_dayNumber; }
    // Recalculates, because the day number now DRIVES something: the moon's phase and therefore its
    // position and its light. It used to be an inert counter, so a plain assignment was harmless.
    // Left inert, the moon lagged one call behind every caller that set the day and the time together
    // (setTimeOfDay recalculates, setDayNumber did not, and the API sets time first) — which rendered
    // a full moon using the PREVIOUS day's phase. Measured as an exact inversion: the "new moon"
    // frame came out lit and the "full moon" frame black.
    void setDayNumber(int day) { m_dayNumber = day; recalculate(); }
    bool isNight() const { return m_timeOfDay >= 18.0f || m_timeOfDay < 6.0f; }
    bool isDay() const { return !isNight(); }

    // Current computed lighting values
    glm::vec3 getSunDirection() const { return m_sunDirection; }
    glm::vec3 getSunColor() const { return m_sunColor; }
    float getAmbientStrength() const { return m_ambientStrength; }
    glm::vec3 getSkyColor() const { return m_skyColor; }  // background sky tint by time of day

    // ---- Moon (2026-08-10) --------------------------------------------------------------------
    /// Direction the MOONLIGHT TRAVELS, same convention as getSunDirection() (so both need flipping
    /// before they go to Atmosphere::/atmosphere.glsl, which want a vector pointing AT the body).
    glm::vec3 getMoonDirection() const { return m_moonDirection; }
    /// Position in the synodic cycle: 0 = new, 0.5 = full, wrapping at 1. Derived from the day
    /// number over WorldClock's LUNAR_CYCLE_DAYS so the renderer and the calendar agree.
    float getMoonPhase01() const { return m_moonPhase01; }
    /// Illuminated fraction of the disc, 0 at new and 1 at full.
    float getMoonIlluminatedFraction() const;

    // Serialization
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    void recalculate();

    float m_timeOfDay;         // 0.0–24.0 hours
    int m_dayNumber;           // Day counter (increments on midnight wrap)
    float m_dayLengthSeconds;  // Real seconds for one full day
    float m_timeScale;         // Speed multiplier
    bool m_enabled;            // Whether cycle updates at all
    bool m_paused;             // Temporarily paused

    // Computed values
    glm::vec3 m_sunDirection;
    glm::vec3 m_sunColor;
    float m_ambientStrength;
    glm::vec3 m_skyColor;
    // The moon shares the sun's swing plane and simply LAGS it by the phase angle, so a full moon
    // (phase 0.5, half a cycle behind) is 180 degrees from the sun and therefore rises as the sun
    // sets — the correct behaviour falls out of the geometry instead of being scripted.
    glm::vec3 m_moonDirection{0.0f, -1.0f, 0.0f};
    float m_moonPhase01 = 0.0f;
};

} // namespace Graphics
} // namespace Phyxel
