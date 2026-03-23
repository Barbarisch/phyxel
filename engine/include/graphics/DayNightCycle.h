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

    // Current computed lighting values
    glm::vec3 getSunDirection() const { return m_sunDirection; }
    glm::vec3 getSunColor() const { return m_sunColor; }
    float getAmbientStrength() const { return m_ambientStrength; }

    // Serialization
    nlohmann::json toJson() const;
    void fromJson(const nlohmann::json& j);

private:
    void recalculate();

    float m_timeOfDay;         // 0.0–24.0 hours
    float m_dayLengthSeconds;  // Real seconds for one full day
    float m_timeScale;         // Speed multiplier
    bool m_enabled;            // Whether cycle updates at all
    bool m_paused;             // Temporarily paused

    // Computed values
    glm::vec3 m_sunDirection;
    glm::vec3 m_sunColor;
    float m_ambientStrength;
};

} // namespace Graphics
} // namespace Phyxel
