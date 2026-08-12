#include "graphics/Atmosphere.h"

#include <algorithm>
#include <cmath>

namespace Phyxel {
namespace Graphics {
namespace Atmosphere {

namespace {

constexpr float kPi = 3.14159265358979323846f;

/// Distance along a ray from `origin` in unit direction `dir` to the sphere of radius `radius`
/// centred at the origin of coordinates. Returns the FAR positive root, or -1 if the ray misses.
/// The planet/atmosphere are concentric shells centred on the planet's centre, so both intersection
/// queries share this.
float raySphereFar(const glm::vec3& origin, const glm::vec3& dir, float radius) {
    const float b = glm::dot(origin, dir);
    const float c = glm::dot(origin, origin) - radius * radius;
    const float disc = b * b - c;
    if (disc < 0.0f) return -1.0f;
    return -b + std::sqrt(disc);
}

/// Does the ray hit the ground before leaving the atmosphere?
bool hitsPlanet(const glm::vec3& origin, const glm::vec3& dir) {
    const float b = glm::dot(origin, dir);
    const float c = glm::dot(origin, origin) - kPlanetRadius * kPlanetRadius;
    const float disc = b * b - c;
    // A ray only hits the ground if it points inward (b < 0) AND the line reaches the sphere.
    return disc >= 0.0f && b < 0.0f;
}

struct Densities {
    float rayleigh;
    float mie;
    float ozone;
};

Densities densitiesAt(float altitudeM) {
    const float h = std::max(altitudeM, 0.0f);
    Densities d{};
    d.rayleigh = std::exp(-h / kRayleighScaleHeight);
    d.mie      = std::exp(-h / kMieScaleHeight);
    // Tent profile: 1 at the layer centre, falling linearly to 0 at +/- kOzoneWidth.
    d.ozone = std::max(0.0f, 1.0f - std::fabs(h - kOzoneCenter) / kOzoneWidth);
    return d;
}

/// Total extinction coefficient (per metre, per channel) at an altitude.
glm::vec3 extinctionAt(float altitudeM) {
    const Densities d = densitiesAt(altitudeM);
    return kRayleighScattering * d.rayleigh
         + glm::vec3(kMieExtinction) * d.mie
         + kOzoneAbsorption * d.ozone;
}

float altitudeOf(const glm::vec3& p) {
    return glm::length(p) - kPlanetRadius;
}

/// Quadratic step schedule. Density falls off exponentially with altitude, so almost all of the
/// scattering along a ray happens in its first few kilometres — uniform steps spend most of their
/// samples in near-vacuum and badly under-resolve the dense part, which shows up precisely as a
/// banded, wrong-coloured horizon at low sun. Clustering samples near the viewer fixes the case we
/// care most about (sunrise/sunset) for the cost of one multiply.
/// Returns the segment [t0, t1] for step i of `steps` over a ray of length `far`.
inline void stepRange(int i, int steps, float far, float& t0, float& t1) {
    const float a = static_cast<float>(i) / static_cast<float>(steps);
    const float b = static_cast<float>(i + 1) / static_cast<float>(steps);
    t0 = far * a * a;
    t1 = far * b * b;
}

/// Optical depth accumulated from `p` along `dir` until the ray leaves the atmosphere.
glm::vec3 opticalDepthOut(const glm::vec3& p, const glm::vec3& dir, int steps) {
    const float far = raySphereFar(p, dir, kAtmosphereRadius);
    if (far <= 0.0f) return glm::vec3(0.0f);
    glm::vec3 tau(0.0f);
    for (int i = 0; i < steps; ++i) {
        float t0, t1;
        stepRange(i, steps, far, t0, t1);
        const glm::vec3 s = p + dir * (0.5f * (t0 + t1));
        tau += extinctionAt(altitudeOf(s)) * (t1 - t0);
    }
    return tau;
}

/// Rayleigh phase: symmetric, mild forward/backward lobes.
float phaseRayleigh(float mu) {
    return (3.0f / (16.0f * kPi)) * (1.0f + mu * mu);
}

/// Cornette-Shanks Mie phase: the strong forward lobe that produces the glow around the sun.
float phaseMie(float mu, float g) {
    const float g2 = g * g;
    const float num = 3.0f * (1.0f - g2) * (1.0f + mu * mu);
    const float den = 8.0f * kPi * (2.0f + g2) * std::pow(1.0f + g2 - 2.0f * g * mu, 1.5f);
    return num / den;
}

glm::vec3 viewerPosition(float altitudeM) {
    return glm::vec3(0.0f, kPlanetRadius + std::max(altitudeM, 1.0f), 0.0f);
}

/// Smooth the horizon crossing so the directional light fades instead of snapping to black the
/// instant the sun's centre passes 0 elevation. The band is roughly the sun's angular diameter,
/// which is also about the size of the real refraction/limb effect.
float horizonFade(float sinElevation) {
    // PHYSICAL radius, deliberately not the stylized drawn one: how fast sunlight dies as the sun
    // dips is set by the real disc crossing the real horizon, and must not change because we chose
    // to draw a bigger sun. See the size note in Atmosphere.h.
    const float band = kSunPhysicalAngularRadius * 2.0f;
    const float t = glm::clamp((sinElevation + band) / (2.0f * band), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);   // smoothstep
}

}  // namespace

glm::vec3 transmittanceToSun(const glm::vec3& toSun, float altitudeM) {
    const glm::vec3 p = viewerPosition(altitudeM);
    const float fade = horizonFade(toSun.y);
    if (fade <= 0.0f) return glm::vec3(0.0f);
    // Below the geometric horizon the path would pass through the planet; the fade above has
    // already taken us to ~0 by then, so the march is only ever run on grazing-or-higher rays.
    const glm::vec3 tau = opticalDepthOut(p, toSun, kCpuSunSteps);
    return glm::exp(-tau) * fade;
}

glm::vec3 sunlightColor(const glm::vec3& toSun, float altitudeM) {
    return kSolarIrradiance * transmittanceToSun(toSun, altitudeM);
}

float moonIlluminatedFraction(float phase01) {
    const float p = phase01 - std::floor(phase01);
    return 0.5f * (1.0f - std::cos(2.0f * kPi * p));
}

glm::vec3 moonlightColor(const glm::vec3& toMoon, float phase01, float altitudeM) {
    const float lit = moonIlluminatedFraction(phase01);
    if (lit <= 0.0f) return glm::vec3(0.0f);
    // Sunlight -> lunar albedo -> the same atmospheric extinction any light from that direction
    // suffers -> the scotopic blue bias -> the explicit look scale.
    return kSolarIrradiance * kMoonAlbedo * lit * kMoonlightScale * kMoonlightTint
         * transmittanceToSun(toMoon, altitudeM);
}

glm::vec3 skyRadiance(const glm::vec3& dirIn, const glm::vec3& toSun, float altitudeM) {
    const glm::vec3 dir = glm::normalize(dirIn);
    const glm::vec3 p = viewerPosition(altitudeM);

    // March to the atmosphere top, or to the ground for downward rays so a below-horizon direction
    // returns the short, dense, near-ground scatter rather than a full-shell integral.
    float far = raySphereFar(p, dir, kAtmosphereRadius);
    if (far <= 0.0f) return glm::vec3(0.0f);
    if (hitsPlanet(p, dir)) {
        const float b = glm::dot(p, dir);
        const float c = glm::dot(p, p) - kPlanetRadius * kPlanetRadius;
        const float disc = b * b - c;
        if (disc >= 0.0f) far = std::min(far, std::max(0.0f, -b - std::sqrt(disc)));
    }

    const float mu = glm::dot(dir, toSun);
    const float pr = phaseRayleigh(mu);
    const float pm = phaseMie(mu, kMieAnisotropy);

    glm::vec3 tauView(0.0f);
    glm::vec3 sumR(0.0f), sumM(0.0f);

    for (int i = 0; i < kCpuViewSteps; ++i) {
        float t0, t1;
        stepRange(i, kCpuViewSteps, far, t0, t1);
        const float ds = t1 - t0;
        const glm::vec3 s = p + dir * (0.5f * (t0 + t1));
        const float h = altitudeOf(s);
        const Densities d = densitiesAt(h);

        // Extinction from the viewer to this sample (trapezoid-free: accumulate at the sample).
        tauView += extinctionAt(h) * ds;

        // Is this sample in sunlight? If the sun ray from here hits the planet, the sample is in
        // the Earth's own shadow — which is exactly what makes twilight fade from the horizon
        // upward instead of the whole sky dimming at once.
        if (hitsPlanet(s, toSun)) continue;

        const glm::vec3 tauSun = opticalDepthOut(s, toSun, kCpuSunSteps);
        const glm::vec3 t = glm::exp(-tauView - tauSun);
        sumR += t * d.rayleigh * ds;
        sumM += t * d.mie * ds;
    }

    return kSolarIrradiance * (sumR * kRayleighScattering * pr
                             + sumM * glm::vec3(kMieScattering) * pm);
}

glm::vec3 skyIrradiance(const glm::vec3& toSun, float altitudeM) {
    // Cosine-weighted hemispheric average over a small fixed direction set. A coarse quadrature is
    // fine here: this feeds a broad ambient fill, not a specular reflection, and it runs once per
    // frame rather than per pixel. Rings at 15/45/75 degrees elevation with 8 azimuths each,
    // weighted by cos(theta) (the Lambert term) — plus the zenith.
    constexpr int kAzimuths = 8;
    const float elevations[3] = {15.0f, 45.0f, 75.0f};
    glm::vec3 sum(0.0f);
    float weight = 0.0f;
    for (float elevDeg : elevations) {
        const float e = glm::radians(elevDeg);
        const float sinE = std::sin(e), cosE = std::cos(e);
        for (int a = 0; a < kAzimuths; ++a) {
            const float az = (2.0f * kPi * static_cast<float>(a)) / static_cast<float>(kAzimuths);
            const glm::vec3 d(cosE * std::cos(az), sinE, cosE * std::sin(az));
            sum += skyRadiance(d, toSun, altitudeM) * sinE;   // sinE == cos(zenith angle)
            weight += sinE;
        }
    }
    sum += skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), toSun, altitudeM);
    weight += 1.0f;
    return sum / std::max(weight, 1e-6f);
}

glm::vec3 hazeHorizon(const glm::vec3& toSun, float altitudeM) {
    // Sample just above the geometric horizon: exactly at 0 the march is degenerate.
    return skyRadiance(glm::vec3(1.0f, 0.02f, 0.0f), toSun, altitudeM);
}

glm::vec3 hazeZenith(const glm::vec3& toSun, float altitudeM) {
    return skyRadiance(glm::vec3(0.0f, 1.0f, 0.0f), toSun, altitudeM);
}

}  // namespace Atmosphere
}  // namespace Graphics
}  // namespace Phyxel
