#include "graphics/CelestialBody.h"

#include "graphics/Atmosphere.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace Phyxel {
namespace Graphics {

namespace {

/// Place a body on its orbit. The base swing matches DayNightCycle's original sun so a
/// period-1 body with zero offset and zero tilt is EXACTLY the sun the engine always had:
///   hourAngle = (t/24)*2pi - pi/2, and the sun rides (-cos*0.7, -sin, -cos*0.3) as a travel
///   direction, i.e. (cos*0.7, sin, cos*0.3) pointing TOWARD it.
/// Period and phase offset advance that angle; plane tilt rotates the resulting arc about the axis
/// the sun travels along, so a tilted body crosses the sky at a different angle rather than sliding
/// along the same rail.
glm::vec3 orbitDirection(const CelestialBody& b, float timeOfDayHours, int dayNumber) {
    const float twoPi = glm::two_pi<float>();
    // Continuous time in DAYS, so a period other than 1.0 drifts across days instead of resetting
    // at midnight — which is exactly what makes a moon's phase advance.
    const float days = static_cast<float>(dayNumber) + timeOfDayHours / 24.0f;
    const float turns = (b.periodDays > 1e-6f) ? (days / b.periodDays) : days;
    const float hourAngle = turns * twoPi - glm::half_pi<float>() - twoPi * b.phaseOffset;

    const float s = std::sin(hourAngle);
    const float c = std::cos(hourAngle);
    glm::vec3 dir = glm::normalize(glm::vec3(c * 0.7f, s, c * 0.3f));

    if (std::fabs(b.planeTilt) > 1e-6f) {
        // Rotate about the horizontal axis the base arc runs along, so the tilt opens the arc out of
        // the sun's plane rather than just spinning it in place.
        const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 0.0f, -0.7f));
        const float ct = std::cos(b.planeTilt), st = std::sin(b.planeTilt);
        dir = dir * ct + glm::cross(axis, dir) * st + axis * glm::dot(axis, dir) * (1.0f - ct);
        dir = glm::normalize(dir);
    }
    return dir;
}

float luma(const glm::vec3& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

}  // namespace

SkyBodies SkyBodies::defaultSky() {
    SkyBodies s;

    CelestialBody sun;
    sun.name = "sun";
    sun.angularRadius = Atmosphere::kSunAngularRadius;
    sun.discBrightness = 24.0f;
    sun.tint = glm::vec3(1.0f);
    sun.emissive = true;
    sun.periodDays = 1.0f;
    sun.phaseOffset = 0.0f;
    s.bodies.push_back(sun);

    CelestialBody moon;
    moon.name = "moon";
    moon.angularRadius = Atmosphere::kMoonAngularRadius;
    moon.discBrightness = 2.2f;
    moon.tint = Atmosphere::kMoonlightTint;
    moon.emissive = false;
    moon.litBy = 0;
    moon.albedo = Atmosphere::kMoonAlbedo;
    moon.lightScale = Atmosphere::kMoonlightScale;
    // Shares the sun's period, so its phase offset IS its lunar phase and it advances across days
    // only because dayNumber advances. A period of exactly 1 would freeze the phase, so the moon
    // runs very slightly slow — one full synodic cycle per LUNAR_CYCLE_DAYS.
    constexpr float kLunarCycleDays = 28.0f;
    moon.periodDays = kLunarCycleDays / (kLunarCycleDays - 1.0f);
    moon.phaseOffset = 0.0f;
    s.bodies.push_back(moon);

    return s;
}

void SkyBodies::update(float timeOfDayHours, int dayNumber, float altitudeM) {
    const size_t n = bodies.size();
    directions.assign(n, glm::vec3(0.0f, 1.0f, 0.0f));
    lightColors.assign(n, glm::vec3(0.0f));
    litFractions.assign(n, 1.0f);

    for (size_t i = 0; i < n; ++i) {
        directions[i] = orbitDirection(bodies[i], timeOfDayHours, dayNumber);
    }

    for (size_t i = 0; i < n; ++i) {
        const CelestialBody& b = bodies[i];

        if (b.emissive) {
            litFractions[i] = 1.0f;
            if (b.castsLight) {
                lightColors[i] = Atmosphere::sunlightColor(directions[i], altitudeM)
                               * b.tint * b.lightScale;
            }
            continue;
        }

        // Reflective: the phase falls out of the ANGLE to whatever lights it. No phase parameter,
        // so the drawn terminator and the light delivered can never disagree with the sky positions.
        int src = b.litBy;
        if (src < 0 || src >= static_cast<int>(n)) {
            src = -1;
            for (size_t j = 0; j < n; ++j) {
                if (bodies[j].emissive) { src = static_cast<int>(j); break; }
            }
        }
        if (src < 0) { litFractions[i] = 0.0f; continue; }

        const float cosSep = glm::clamp(glm::dot(directions[i], directions[src]), -1.0f, 1.0f);
        // Facing the same way as its light source = new (dark); opposite = full.
        const float lit = 0.5f * (1.0f - cosSep);
        litFractions[i] = lit;

        if (b.castsLight && lit > 0.0f) {
            // ⚠️ The light falling on a reflective body is sunlight IN SPACE — top-of-atmosphere
            // irradiance — NOT sunlightColor(), which is what survives the trip down to the viewer.
            // Using the latter zeroes the moon precisely at night, when the sun is below OUR horizon
            // but is still perfectly well illuminating the moon. (Caught by
            // DominantLightIsTheSunByDayAndTheMoonAtNight: a full moon at midnight went black.)
            // Only the reflected light's own trip down through the atmosphere is attenuated, via
            // this body's transmittance.
            const glm::vec3 incident = Atmosphere::kSolarIrradiance
                                     * bodies[src].tint * bodies[src].lightScale;
            lightColors[i] = incident * b.albedo * lit * b.lightScale * b.tint
                           * Atmosphere::transmittanceToSun(directions[i], altitudeM);
        }
    }
}

int SkyBodies::dominantLightIndex() const {
    int best = -1;
    float bestLuma = 0.0f;
    for (size_t i = 0; i < lightColors.size(); ++i) {
        if (i < directions.size() && directions[i].y <= 0.0f) continue;   // below the horizon
        const float l = luma(lightColors[i]);
        if (l > bestLuma) { bestLuma = l; best = static_cast<int>(i); }
    }
    return best;
}

}  // namespace Graphics
}  // namespace Phyxel
