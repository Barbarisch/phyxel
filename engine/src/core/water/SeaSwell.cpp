#include "core/water/SeaSwell.h"

#include <cmath>

namespace Phyxel::Core::Water {

void seaSwellComponents(const SeaSwellParams& p, SeaSwellComponent out[4]) {
    const float w = p.windRad;
    out[0] = {glm::vec2(std::cos(w), std::sin(w)), p.amplitude, p.wavelength};
    out[1] = {glm::vec2(std::cos(w + 0.6f), std::sin(w + 0.6f)), p.amplitude * 0.52f, p.wavelength * 0.61f};
    out[2] = {glm::vec2(std::cos(w - 0.9f), std::sin(w - 0.9f)), p.amplitude * 0.28f, p.wavelength * 0.33f};
    out[3] = {glm::vec2(std::cos(w + 0.25f), std::sin(w + 0.25f)), p.amplitude * 0.70f, p.wavelength * 5.0f};
}

float seaSwellHeight(const SeaSwellParams& p, float x, float z, float t) {
    if (p.amplitude <= 0.0f || p.wavelength <= 0.0f) return 0.0f;
    SeaSwellComponent c[4];
    seaSwellComponents(p, c);
    float h = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const float k = 6.28318530718f / c[i].wavelength;
        const float cph = std::sqrt(9.81f / k);                       // deep-water phase speed, as water.vert
        const float f = k * (c[i].dir.x * x + c[i].dir.y * z - cph * t);
        h += c[i].amplitude * std::sin(f);
    }
    return h;
}

SeaSwellSample seaSwellSample(const SeaSwellParams& p, float x, float z, float t, float depth) {
    SeaSwellSample s;
    if (p.amplitude <= 0.0f || p.wavelength <= 0.0f) return s;
    SeaSwellComponent c[4];
    seaSwellComponents(p, c);
    const float d = std::max(depth, 0.1f);
    for (int i = 0; i < 4; ++i) {
        const float k = 6.28318530718f / c[i].wavelength;
        const float cph = std::sqrt(9.81f / k);
        const float omega = k * cph;
        const float f = k * (c[i].dir.x * x + c[i].dir.y * z - cph * t);
        const float sn = std::sin(f), cs = std::cos(f);
        s.height += c[i].amplitude * sn;
        // Airy: u_surface = a omega cosh(kd) / sinh(kd) cos(theta) along the travel direction, w_surface = a omega sin(theta)
        const float kd = k * d;
        const float coth = std::cosh(kd) / std::max(std::sinh(kd), 1e-6f);
        const float us = c[i].amplitude * omega * coth * sn;   // the horizontal velocity is in phase with the elevation for a progressive wave
        s.uSurface += c[i].dir * us;
        s.wSurface += c[i].amplitude * omega * cs;
        if (i == 0) s.k = k;
    }
    return s;
}

SeaSwellColumn seaSwellColumn(const SeaSwellParams& p, float x, float z, float t, float depth) {
    SeaSwellColumn s;
    if (p.amplitude <= 0.0f || p.wavelength <= 0.0f) return s;
    SeaSwellComponent c[4];
    seaSwellComponents(p, c);
    const float d = std::max(depth, 0.05f);
    // depth-limited: the four components' total height 2 sum(a_i) may not exceed gamma d (McCowan,
    // gamma 0.78) - beyond that the wave has broken and linear theory has nothing to say; the ring
    // then prescribes the saturated wave the shallows can hold (the sheet beside it still draws the
    // full swell: the documented seam, WaterCore.md 18.5)
    // on the four components' TOTAL height (the rms height was tried: the near-breaking crests then
    // ran the Froude clamp and the ring pumped 0.125 m of set-up into the band test)
    float total = 0.0f;
    for (int i = 0; i < 4; ++i) total += c[i].amplitude;
    const float scale = std::min(1.0f, 0.78f * d / std::max(2.0f * total, 1e-6f));
    s.depthScale = scale;
    for (int i = 0; i < 4; ++i) {
        const float k = 6.28318530718f / c[i].wavelength;
        const float cph = std::sqrt(9.81f / k);                           // the sheet's deep-water phase: the SURFACE it draws
        const float f = k * (c[i].dir.x * x + c[i].dir.y * z - cph * t);
        const float a = c[i].amplitude * scale;
        const float eta = a * std::sin(f);
        s.height += eta;
        const float cd = std::sqrt(9.81f / k * std::tanh(k * d));        // Airy at this depth: the transport belongs to the depth
        s.uAvg += c[i].dir * (eta * cd / d);
        s.uAvg -= c[i].dir * (a * a * cd / (2.0f * d * d));   // minus the Stokes transport: the ring pumps no net mass
    }
    const float froude = std::sqrt(9.81f * d), speed = glm::length(s.uAvg);   // never faster than the shallow-water wave: linear theory ends where the wave breaks
    if (speed > froude) s.uAvg *= froude / speed;
    return s;
}

}  // namespace Phyxel::Core::Water
