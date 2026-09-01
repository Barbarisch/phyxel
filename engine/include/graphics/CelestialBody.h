#pragma once

// CelestialBody.h — the sky's inhabitants, as DATA rather than as two hardcoded special cases.
//
// WHY. The sun and the moon were separate code paths that happened to share a scattering model: the
// sun had a direction and a colour, the moon had a direction and a phase, and "two moons" or "a
// binary sun" would each have meant new code. They are not actually different things. A body is a
// disc in the sky with a size, an orbit, and a way of getting its light — either it EMITS (a star)
// or it REFLECTS another body's light (a moon). Everything else follows from those.
//
// So this replaces both with one list. A single-sun/single-moon sky is just the default two entries,
// and an alien sky with two moons on different orbits is configuration, not a code change.
//
// ── THE THREE THINGS A BODY NEEDS ──────────────────────────────────────────────────────────────
//
// SIZE is a stylized choice, deliberately. Real bodies are ~0.5 degrees across — a ten-pixel dot at
// a typical field of view, at which a moon's phase is invisible. `angularRadius` is authored in
// radians and defaults to 5x life. Note the split that must be preserved: how large a body is DRAWN
// never affects how its light behaves (see kHorizonFadeRadius in Atmosphere.h).
//
// ORBIT is period + phase offset + plane tilt, which is the smallest set that produces a sky worth
// looking at. Period gives a fast inner moon and a slow outer one; phase offset places them apart;
// plane tilt makes them trace visibly different arcs instead of sliding along one rail. Deliberately
// NOT full orbital elements — eccentricity and ascending nodes are hard to author by hand, hard to
// test, and buy nothing you can see from the ground.
//
// LIGHT ROLE. `emissive` bodies make their own light. Reflective bodies are lit by `litBy`, which is
// what produces a phase: the illuminated fraction falls out of the angle between this body and the
// one lighting it, so a full moon rises at sunset because the geometry says so rather than because
// a parameter was set.
//
// ⚠️ ONLY ONE BODY CAN CAST SHADOWS. The three shadow cascades are fitted to a single light
// direction. The rule (see dominantLightIndex) is that the brightest body currently above the
// horizon owns the cascades and every other light-contributing body adds UNSHADOWED light. That is
// the same rule that already governed sun-versus-moon, generalised — it is not a new compromise.

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace Phyxel {
namespace Graphics {

/// One sun, moon, or other disc in the sky.
struct CelestialBody {
    std::string name = "sun";

    // ---- Appearance ----------------------------------------------------------------------------
    /// Drawn angular RADIUS in radians. Life size is ~0.00467 (sun) / ~0.00453 (moon); the default
    /// here is 5x that, which is a look decision — see the header note.
    float angularRadius = 0.023375f;
    /// Multiplier on the drawn disc's brightness. Not physical: the sun's true radiance is ~14,000x
    /// a lit surface, which nothing downstream could hold. Tune it against the tonemapper.
    float discBrightness = 24.0f;
    /// Colour multiplier for both the disc and the light it gives. For a reflective body this is its
    /// surface tint (a grey rock reads slightly blue to night vision, hence the moon's default).
    glm::vec3 tint{1.0f, 1.0f, 1.0f};

    // ---- Light role ----------------------------------------------------------------------------
    /// True = a star, making its own light. False = lit by `litBy`, and therefore has phases.
    bool emissive = true;
    /// Index into the body list of whatever illuminates this one. Ignored when `emissive`.
    /// -1 means "the first emissive body", which is what a moon almost always wants.
    int litBy = -1;
    /// Fraction of incident light this body reflects. Only meaningful when reflective; the Moon's
    /// Bond albedo is ~0.12.
    float albedo = 0.12f;
    /// Scales the light this body contributes to the WORLD (not its drawn disc). Physically a full
    /// moon is ~1/400,000 of sunlight, which is true and useless; this is the honest cheat knob.
    float lightScale = 1.0f;
    /// If false the body is drawn but contributes NO light — useful for a purely decorative
    /// companion, or for a distant sun you want visible without it brightening the world.
    bool castsLight = true;

    // ---- Orbit ---------------------------------------------------------------------------------
    /// Days for one full circuit of the sky. 1.0 = once per in-game day (a sun). A moon's apparent
    /// motion comes from a period slightly different from the day, which is why it drifts.
    float periodDays = 1.0f;
    /// Where in its circuit the body starts, in turns (0..1). For a body sharing the sun's period,
    /// this IS its phase: 0.5 puts it opposite the sun, i.e. full and rising at sunset.
    float phaseOffset = 0.0f;
    /// Tilt of the orbital plane away from the sun's, in radians. 0 = the same arc as the sun;
    /// non-zero makes the body cross the sky at a visibly different angle.
    float planeTilt = 0.0f;
};

/// The sky's full complement of bodies, plus the per-frame results of placing them.
struct SkyBodies {
    std::vector<CelestialBody> bodies;

    /// Unit vector pointing FROM the viewer TOWARD each body, index-matched to `bodies`.
    /// (The same convention Atmosphere:: uses — NOT the direction light travels.)
    std::vector<glm::vec3> directions;
    /// Light each body delivers to the world: colour and intensity, already including its phase,
    /// albedo, lightScale and atmospheric extinction. Zero for a body below the horizon, a new moon,
    /// or one with castsLight false.
    std::vector<glm::vec3> lightColors;
    /// Illuminated fraction of the visible disc, 0..1. Always 1 for an emissive body.
    std::vector<float> litFractions;

    /// The default sky: one sun, one moon half a cycle behind it. Reproduces the engine's original
    /// hardcoded pair exactly, so an unconfigured world looks the way it always did.
    static SkyBodies defaultSky();

    /// Place every body for a time of day and day number, filling directions / lightColors /
    /// litFractions. `altitudeM` is the viewer's height for atmospheric extinction.
    void update(float timeOfDayHours, int dayNumber, float altitudeM = 1.0f);

    /// Parse a sky definition. Shape (every field optional, defaults from CelestialBody):
    ///   { "bodies": [ { "name": "sun", "angularDiameterDeg": 2.68, "discBrightness": 24,
    ///                   "tint": [1,1,1], "emissive": true, "periodDays": 1.0,
    ///                   "phaseOffset": 0.0, "planeTiltDeg": 0.0 },
    ///                 { "name": "moon", "emissive": false, "litBy": 0, "albedo": 0.12,
    ///                   "lightScale": 0.25, "periodDays": 1.037, "phaseOffset": 0.0 } ] }
    ///
    /// Size is authored as an angular DIAMETER IN DEGREES, not radians: radians are unreadable in a
    /// config file, and degrees make the stylized choice legible (the real sun and moon are ~0.5;
    /// the default here is 2.68, i.e. 5x life). An empty or missing "bodies" array yields
    /// defaultSky() rather than an empty sky, because a world with no sun is never what was meant.
    static SkyBodies fromJson(const nlohmann::json& j);
    nlohmann::json toJson() const;

    /// Multiply every body's drawn size by `scale` (1.0 = leave alone). The one-knob answer to
    /// "make them bigger", without having to restate the whole list.
    void scaleAllSizes(float scale);

    /// Index of the body that should own the shadow cascades: the brightest light-contributing body
    /// currently above the horizon. Returns -1 when nothing is up (true night with no moon), in
    /// which case the caller should leave the cascades alone rather than fit them to nothing.
    /// ⚠️ Only ONE body can cast shadows — the cascades are fitted to a single direction.
    int dominantLightIndex() const;
};

}  // namespace Graphics
}  // namespace Phyxel
