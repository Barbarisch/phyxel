#pragma once

#include "core/DayNightSettings.h"

#include <chrono>
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

    /// Advance by the REAL time since the previous tick, measured from this object's own last
    /// stamp. The first tick advances nothing; a stall (or debugger pause) advances at most
    /// kMaxTickSeconds so a 5 s hitch does not skip an hour.
    ///
    /// This exists because the frame loop used to compute dt from a frame-start stamp that only
    /// the EDITOR set. A shipped game never set it, `now - epoch` hit the clamp every frame, and
    /// the clock ran at 0.1 s x FPS: measured 2.89x real time at 28.2 FPS (G-165). A clock that
    /// times itself cannot be broken by a host forgetting to stamp it.
    using Clock = std::chrono::steady_clock;
    static constexpr float kMaxTickSeconds = 0.1f;
    void tick(Clock::time_point now);

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

    /// Apply an authored block (G-165). Only the fields the block SETS change; the rest keep
    /// their current values, so `{"enabled": true}` alone turns the cycle on at the engine
    /// defaults. Order matters and is fixed here: day length and scale before time of day, and
    /// day number before time of day, because setTimeOfDay() recalculates the sun and moon from
    /// whatever day number is current (see setDayNumber's note).
    void applySettings(const Core::DayNightSettings& s);

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
    bool m_hasTicked = false;  // tick(): no previous stamp until the first call
    Clock::time_point m_lastTick{};

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
