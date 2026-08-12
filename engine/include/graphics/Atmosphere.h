#pragma once

// Atmosphere.h — THE physical model of the sky, and therefore of the light.
//
// WHY THIS EXISTS. The engine's sun colour, sky colour and distance-haze colour were three
// independently hand-tuned constant ramps in DayNightCycle and lighting.glsl. Nothing tied them to
// each other, so "the sun looks warm" and "the light is warm" were two separate tuning jobs that
// drifted — the same per-tier, hand-synced failure that lighting.glsl was created to end. This
// replaces all three with ONE analytic single-scattering model: the sun you see IS the light you get.
//
// THE MODEL. Rayleigh + Mie single scattering with ozone absorption, ray-marched through a
// spherical shell atmosphere. Deliberately analytic rather than LUT-based for now, behind a small
// interface (radiance for a direction, transmittance to the sun). Upgrading to precomputed
// transmittance / multiple-scattering / sky-view LUTs (Hillaire 2020) later replaces the BODIES of
// these functions and none of their callers.
//
// Why ozone is in a "simple" model: ozone absorbs in the Chappuis band, which is what makes the
// twilight sky BLUE instead of grey after the sun has set. Leaving it out is the single most common
// reason an otherwise correct scattering model produces a muddy, disappointing dusk. It costs one
// extra density term.
//
// ⚠️ DIRECTION CONVENTION. Every function here takes `toSun` / `toMoon`: a normalised vector
// pointing FROM the viewer TOWARDS the body, so `toSun.y == sin(elevation)`. The engine's
// `ubo.sunDirection` is the opposite — the direction light TRAVELS (down at noon) — and shaders
// already flip it (`sunL = normalize(-ubo.sunDirection)`). Passing the unflipped vector here yields
// a permanent midnight, so the flip is the caller's job and is asserted in the tests.
//
// ⚠️ TWO IMPLEMENTATIONS, ONE SET OF CONSTANTS. shaders/atmosphere.glsl mirrors this file for
// rendering. `AtmosphereTest` PARSES the .glsl and asserts every shared constant matches the value
// here, because this repository has repeatedly been bitten by hand-synced duplicates (two
// InstanceData structs, five copies of the lighting model). Change a constant here and the test
// tells you the shader is stale.

#include <glm/glm.hpp>

namespace Phyxel {
namespace Graphics {
namespace Atmosphere {

// ---- Shared constants. MIRRORED IN shaders/atmosphere.glsl (parity is unit-tested). -------------
// Earth-scale geometry, in METRES. World units are voxels; the atmosphere is modelled at real scale
// and the camera's altitude is mapped in, because scattering only looks right at physical depths.
constexpr float kPlanetRadius     = 6360000.0f;   // m, ground
constexpr float kAtmosphereRadius = 6420000.0f;   // m, top of atmosphere (60 km shell)

// Rayleigh scattering at sea level, per metre, per channel. Blue scatters ~5.7x more than red,
// which is why the sky is blue and why a long path (low sun) leaves only red.
constexpr glm::vec3 kRayleighScattering{5.802e-6f, 13.558e-6f, 33.100e-6f};
constexpr float     kRayleighScaleHeight = 8000.0f;    // m

// Mie: aerosols. Nearly wavelength-independent, strongly forward-scattering — the white haze near
// the horizon and the glow around the sun.
constexpr float kMieScattering    = 3.996e-6f;   // per m
constexpr float kMieExtinction    = 4.400e-6f;   // per m (scattering + absorption)
constexpr float kMieScaleHeight   = 1200.0f;     // m
constexpr float kMieAnisotropy    = 0.80f;       // g, Cornette-Shanks

// Ozone: absorption only, concentrated in a layer around 25 km. A tent profile is plenty.
constexpr glm::vec3 kOzoneAbsorption{0.650e-6f, 1.881e-6f, 0.085e-6f};
constexpr float     kOzoneCenter = 25000.0f;   // m
constexpr float     kOzoneWidth  = 15000.0f;   // m (half-width of the tent)

// Top-of-atmosphere solar irradiance, normalised so a noon sun lands near 1.0 after extinction
// rather than in photometric units — the renderer has no absolute photometric pipeline yet, and a
// unit-less scale keeps this change from silently re-exposing every existing world.
constexpr glm::vec3 kSolarIrradiance{1.0f, 0.97f, 0.92f};

// ---- Apparent size of the sun and moon: a deliberate STYLIZED choice ---------------------------
// The real sun and moon are both about half a degree across, which at a typical field of view is
// roughly a TEN PIXEL dot. Rendered at life size they read as specks, and in the moon's case the
// phase terminator -- which is computed correctly, per pixel, from the sphere's normal against the
// sun -- is entirely invisible. A feature nobody can see is not a feature.
//
// So the DRAWN discs are 5x life size (~2.7 deg across). This is an art decision, not an error, and
// it is the near-universal one: games essentially always oversize both bodies for exactly this
// reason. kSunSizeScale is the single knob; raise it for a more stylized sky, set it to 1 for
// physical size.
//
// ⚠️ THE DRAWN SIZE MUST NOT CHANGE WHEN THE SUN SETS. The horizon fade -- how quickly direct
// sunlight dies as the sun dips -- models the real disc crossing the real horizon, so it uses
// kSunPhysicalAngularRadius and is INDEPENDENT of how large we choose to draw things. Deriving the
// fade band from the stylized radius instead would make sunlight linger ~2.7 deg below the horizon,
// i.e. shadows at dusk, which `AtmosphereTest.NoDirectSunlightBelowTheHorizon` catches.
constexpr float kSunSizeScale = 5.0f;
constexpr float kSunPhysicalAngularRadius  = 0.004675f;   // rad (~0.268 deg) -- the real sun
constexpr float kMoonPhysicalAngularRadius = 0.004525f;   // rad (~0.259 deg) -- the real moon
constexpr float     kSunAngularRadius = kSunPhysicalAngularRadius * kSunSizeScale;

// The moon is lit BY the sun, so its light is sunlight reflected off a dark grey body: Bond albedo
// ~0.12, and the disc is 0.259 deg. The faint blue bias is the Purkinje shift — scotopic vision
// really does read moonlight as cool, and every film convention agrees.
constexpr float     kMoonAngularRadius = kMoonPhysicalAngularRadius * kSunSizeScale;
constexpr float     kMoonAlbedo        = 0.12f;
constexpr glm::vec3 kMoonlightTint{0.62f, 0.78f, 1.0f};
// Full moon illuminance is ~1/400,000 of sunlight. That is physically true and visually useless:
// nothing would be visible, and games universally cheat it. kMoonlightScale is an explicit LOOK
// constant, not a measurement -- the honest label matters so nobody later "fixes" it toward physics.
// CALIBRATED BY MEASUREMENT (2026-08-10): the first value, 0.035, was picked by analogy to physics
// without checking what it produced on screen. Stacked with kMoonAlbedo (0.12) and the restored
// Lambert 1/pi, a full moon lit the ground at ~0.002 display-linear -- indistinguishable from black,
// and a full-moon night measured identical to a new-moon one. 0.25 puts a moonlit surface at roughly
// a fifteenth of daylight: clearly night, clearly visible.
constexpr float kMoonlightScale = 0.25f;

// Sampling. The CPU path runs a handful of times per frame (not per pixel), so it can afford to be
// accurate; the shader uses coarser counts declared in atmosphere.glsl.
constexpr int kCpuViewSteps = 32;
constexpr int kCpuSunSteps  = 12;

// ---- API ---------------------------------------------------------------------------------------

/// Optical-depth transmittance from a point at `altitudeM` toward `toSun`, i.e. the fraction of
/// each wavelength that survives the trip out of the atmosphere. Returns 0 when the ray is blocked
/// by the planet (the sun has set), smoothstepped across the horizon over roughly the sun's own
/// angular diameter so the directional light fades rather than snapping off.
glm::vec3 transmittanceToSun(const glm::vec3& toSun, float altitudeM = 1.0f);

/// The colour and intensity of DIRECT sunlight reaching the viewer: solar irradiance times
/// transmittance. This is what drives the scene's directional light. Near-white overhead; deep
/// orange-red at the horizon, because the long slant path has scattered the blue away. No separate
/// "sunset colour" ramp is needed or wanted — this IS the sunset.
glm::vec3 sunlightColor(const glm::vec3& toSun, float altitudeM = 1.0f);

/// Direct moonlight, as sunlight reflected off the lunar surface and attenuated by the atmosphere.
/// `phase01` is the position in the synodic cycle: 0 = new, 0.5 = full, wrapping at 1 — the natural
/// mapping from WorldClock's day-within-LUNAR_CYCLE_DAYS. Intensity scales with the illuminated
/// fraction, so a new moon genuinely contributes nothing and a full moon night reads differently
/// from a crescent one.
glm::vec3 moonlightColor(const glm::vec3& toMoon, float phase01, float altitudeM = 1.0f);

/// Single-scattered sky radiance looking along `dir` (any direction; downward rays are marched only
/// as far as the ground). This is the same function the sky pass evaluates per pixel.
glm::vec3 skyRadiance(const glm::vec3& dir, const glm::vec3& toSun, float altitudeM = 1.0f);

/// Cosine-weighted average of `skyRadiance` over the upper hemisphere — the physically-derived
/// ambient fill colour, replacing the scalar `ambientLight` times a constant tint.
glm::vec3 skyIrradiance(const glm::vec3& toSun, float altitudeM = 1.0f);

/// Aerial-perspective endpoints. The shader lerps between them by view-direction Y instead of
/// evaluating a full scattering march per fragment, which keeps distance haze matching the sky it
/// sits in front of — near and far tiers inherit the same curve.
glm::vec3 hazeHorizon(const glm::vec3& toSun, float altitudeM = 1.0f);
glm::vec3 hazeZenith(const glm::vec3& toSun, float altitudeM = 1.0f);

/// Illuminated fraction of the lunar disc for a phase in [0,1) (0 = new, 0.5 = full).
/// Exposed so the renderer and any gameplay/UI read the SAME number.
float moonIlluminatedFraction(float phase01);

}  // namespace Atmosphere
}  // namespace Graphics
}  // namespace Phyxel
