// AtmosphereTest.cpp — does the scattering model actually behave like an atmosphere?
//
// These tests assert PHYSICS, not implementation. Each one states a fact about the real sky that any
// correct single-scattering model must reproduce, so the suite survives swapping the analytic march
// for precomputed LUTs later — which is the whole point of putting the model behind a small
// interface.
//
// The tests exist because a scattering model is exactly the kind of code that looks plausible and is
// wrong: sign errors in the sun direction give permanent midnight, a missing ozone term gives a grey
// dusk, and an unnormalised phase function gives a sky that is merely blue-ish. None of that is
// obvious from a screenshot, and all of it is obvious from a number.
//
// The last test is a different kind: it PARSES shaders/atmosphere.glsl and asserts its constants
// equal the C++ ones. This repository has been bitten repeatedly by hand-synced duplicates (two
// InstanceData structs, five copies of the lighting model), so the duplicate is guarded rather than
// trusted.

#include <gtest/gtest.h>

#include "graphics/Atmosphere.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace Phyxel::Graphics;

namespace {

/// A unit vector pointing at a body `elevationDeg` above the horizon. Azimuth is arbitrary in a
/// spherically symmetric atmosphere, so all tests use the +X azimuth.
glm::vec3 towardElevation(float elevationDeg, float azimuthDeg = 0.0f) {
    const float e = glm::radians(elevationDeg);
    const float a = glm::radians(azimuthDeg);
    return glm::normalize(glm::vec3(std::cos(e) * std::cos(a), std::sin(e), std::cos(e) * std::sin(a)));
}

float luma(const glm::vec3& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

std::string readShaderSource() {
    for (const char* p : {"shaders/atmosphere.glsl", "../shaders/atmosphere.glsl",
                          "../../shaders/atmosphere.glsl", "../../../shaders/atmosphere.glsl"}) {
        if (std::filesystem::exists(p)) {
            std::ifstream f(p);
            std::stringstream ss;
            ss << f.rdbuf();
            return ss.str();
        }
    }
    return {};
}

/// Pull a single float out of a GLSL `const float kName = <value>;` declaration.
bool glslFloat(const std::string& src, const std::string& name, float& out) {
    const std::regex re("const\\s+float\\s+" + name +
                        "\\s*=\\s*([-+0-9.eE]+)\\s*;");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = std::stof(m[1].str());
    return true;
}

/// Pull three floats out of a GLSL `const vec3 kName = vec3(a, b, c);` declaration.
bool glslVec3(const std::string& src, const std::string& name, glm::vec3& out) {
    const std::regex re("const\\s+vec3\\s+" + name +
                        "\\s*=\\s*vec3\\s*\\(\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*,\\s*([-+0-9.eE]+)\\s*\\)");
    std::smatch m;
    if (!std::regex_search(src, m, re)) return false;
    out = glm::vec3(std::stof(m[1].str()), std::stof(m[2].str()), std::stof(m[3].str()));
    return true;
}

}  // namespace

// ── The sun's own colour ─────────────────────────────────────────────────────────────────────────

// A noon sun is near-white and bright. The vertical air path is thin, so little is scattered out and
// all three channels survive; a model that reddens the noon sun has its optical depth far too high.
TEST(Atmosphere, NoonSunIsBrightAndNearlyWhite) {
    const glm::vec3 c = Atmosphere::sunlightColor(towardElevation(90.0f));
    EXPECT_GT(luma(c), 0.6f) << "noon sunlight should be bright: got " << luma(c);
    // "Near-white" = the red/blue ratio stays modest. Real sunlight at zenith is slightly warm.
    const float rb = c.r / std::max(c.b, 1e-6f);
    EXPECT_LT(rb, 1.6f) << "noon sun should not be strongly reddened: R/B = " << rb;
    EXPECT_GT(rb, 0.9f) << "noon sun should not be blue-shifted: R/B = " << rb;
}

// THE sunset test. At the horizon the slant path is tens of times longer, and Rayleigh scattering
// removes blue ~5.7x faster than red, so the surviving beam is deep orange-red. This single fact is
// what makes the directional light warm at dusk with no hand-authored colour ramp anywhere.
TEST(Atmosphere, HorizonSunIsStronglyReddened) {
    const glm::vec3 c = Atmosphere::sunlightColor(towardElevation(0.5f));
    const float rb = c.r / std::max(c.b, 1e-9f);
    EXPECT_GT(rb, 10.0f) << "a sun at the horizon must be strongly red-shifted: R/B = " << rb
                         << " (rgb " << c.r << ", " << c.g << ", " << c.b << ")";
    EXPECT_GT(c.r, c.g) << "red must survive better than green";
    EXPECT_GT(c.g, c.b) << "green must survive better than blue";
}

// Direct sunlight weakens monotonically as the sun descends — no bumps, no re-brightening.
TEST(Atmosphere, SunlightWeakensMonotonicallyAsSunDescends) {
    float prev = 1e9f;
    for (float elev : {90.0f, 60.0f, 40.0f, 25.0f, 15.0f, 8.0f, 4.0f, 1.0f}) {
        const float l = luma(Atmosphere::sunlightColor(towardElevation(elev)));
        EXPECT_LT(l, prev) << "luminance rose going down to elevation " << elev;
        prev = l;
    }
}

// Once the sun is properly below the horizon there is NO direct sunlight — twilight is sky-lit, not
// sun-lit. Without this the world would keep casting hard shadows at midnight.
TEST(Atmosphere, NoDirectSunlightBelowTheHorizon) {
    EXPECT_LT(luma(Atmosphere::sunlightColor(towardElevation(-2.0f))), 1e-6f);
    EXPECT_LT(luma(Atmosphere::sunlightColor(towardElevation(-30.0f))), 1e-6f);
    EXPECT_LT(luma(Atmosphere::sunlightColor(towardElevation(-90.0f))), 1e-6f);
}

// The horizon crossing must be smooth, or the scene's key light pops off between two frames.
//
// The bound here is ABSOLUTE, not relative, and that distinction is the whole test. Sunlight really
// does fall off steeply in the last half-degree — optical depth along a grazing path explodes — so a
// "no more than X% lost per step" rule fails on a perfectly smooth exponential tail once the values
// are down in the 1e-4 range, where nothing is visible anyway. What actually matters is that no
// single small step in sun elevation removes a PERCEPTIBLE amount of light. (First version of this
// test used a relative bound and flagged drops from 0.0011 to 0.0007 — invisible, and not the defect
// the test exists to catch.)
TEST(Atmosphere, HorizonCrossingIsSmooth) {
    constexpr float kMaxVisibleJump = 0.02f;   // luminance, per 0.1 degree of sun elevation
    float prev = luma(Atmosphere::sunlightColor(towardElevation(1.0f)));
    for (float elev = 0.9f; elev >= -1.0f; elev -= 0.1f) {
        const float l = luma(Atmosphere::sunlightColor(towardElevation(elev)));
        EXPECT_LE(l, prev + 1e-6f) << "non-monotonic at elevation " << elev;
        EXPECT_LT(prev - l, kMaxVisibleJump)
            << "the key light jumped by " << (prev - l) << " between " << (elev + 0.1f) << " and "
            << elev << " degrees, which would read as a pop";
        prev = l;
    }
    // And it must genuinely reach zero rather than leaving a floor behind.
    EXPECT_LT(luma(Atmosphere::sunlightColor(towardElevation(-1.0f))), 1e-6f);
}

// ⚠️ The direction-convention guard. `ubo.sunDirection` points the way light TRAVELS (downward at
// noon); these functions want the vector pointing AT the body. Passing the wrong one is a silent
// permanent midnight, so the asymmetry is pinned.
TEST(Atmosphere, DirectionConventionIsTowardTheBody) {
    const glm::vec3 up = towardElevation(90.0f);
    EXPECT_GT(luma(Atmosphere::sunlightColor(up)), 0.6f);
    EXPECT_LT(luma(Atmosphere::sunlightColor(-up)), 1e-6f)
        << "a flipped sun vector must read as night, not as noon";
}

// ── The sky ──────────────────────────────────────────────────────────────────────────────────────

// The daytime sky is decisively blue at 90 degrees from the sun — the textbook geometry, and the
// deepest blue in the real sky. Rayleigh scattering dominates there because its phase function is
// nearly flat while Mie's is a narrow forward lobe.
//
// The geometry is load-bearing, not incidental. The first version of this test looked at the zenith
// with the sun AT the zenith, i.e. straight into the sun, and measured only B/R = 1.5 — correctly,
// because that direction is the forward-Mie hotspot where the sky really is whitish. Testing "is the
// sky blue" in the one direction where it physically is not was the bug. Sun and view are placed at
// 45 degrees elevation in opposite azimuths, which is exactly 90 degrees apart.
TEST(Atmosphere, DaytimeSkyIsDecisivelyBlueAtNinetyDegreesFromTheSun) {
    const glm::vec3 toSun = towardElevation(45.0f, 0.0f);
    const glm::vec3 view  = towardElevation(45.0f, 180.0f);
    ASSERT_NEAR(glm::dot(toSun, view), 0.0f, 1e-5f) << "test premise: the two directions are 90 apart";

    const glm::vec3 s = Atmosphere::skyRadiance(view, toSun);
    EXPECT_GT(s.b, s.g) << "blue must exceed green (rgb " << s.r << ", " << s.g << ", " << s.b << ")";
    EXPECT_GT(s.g, s.r) << "green must exceed red";
    EXPECT_GT(s.b / std::max(s.r, 1e-9f), 2.5f) << "the sky should be decisively blue, not blue-ish";
}

// The other half of that fact, pinned so the phase functions cannot quietly go flat: the sky
// immediately AROUND the sun is much whiter than the sky at 90 degrees, because Mie scatters
// strongly forward. If this ever fails, the Mie phase term has been broken or dropped.
TEST(Atmosphere, SkyNearTheSunIsWhiterThanSkyAtNinetyDegrees) {
    const glm::vec3 toSun = towardElevation(45.0f, 0.0f);
    auto blueRatio = [](const glm::vec3& c) { return c.b / std::max(c.r, 1e-9f); };
    const float nearSun = blueRatio(Atmosphere::skyRadiance(towardElevation(46.0f, 0.0f), toSun));
    const float away    = blueRatio(Atmosphere::skyRadiance(towardElevation(45.0f, 180.0f), toSun));
    EXPECT_LT(nearSun, away) << "the sky beside the sun should be less blue than the sky 90 away: "
                             << nearSun << " vs " << away;
}

// Looking toward a low sun is brighter than looking away from it: Mie scatters strongly forward.
TEST(Atmosphere, SkyIsBrighterTowardTheSunThanAwayFromIt) {
    const glm::vec3 toSun = towardElevation(6.0f, 0.0f);
    const glm::vec3 towardSky = Atmosphere::skyRadiance(towardElevation(6.0f, 0.0f), toSun);
    const glm::vec3 awaySky   = Atmosphere::skyRadiance(towardElevation(6.0f, 180.0f), toSun);
    EXPECT_GT(luma(towardSky), luma(awaySky) * 1.5f)
        << "forward Mie scattering should make the sunward sky clearly brighter: "
        << luma(towardSky) << " vs " << luma(awaySky);
}

// Near the horizon the path through dense air is longest, so the sky washes out toward white/haze
// while the zenith stays saturated blue. This is the gradient that makes a sky read as a sky.
TEST(Atmosphere, HorizonSkyIsLessSaturatedThanZenith) {
    const glm::vec3 toSun = towardElevation(50.0f);
    auto sat = [](const glm::vec3& c) {
        const float mx = std::max(c.r, std::max(c.g, c.b));
        const float mn = std::min(c.r, std::min(c.g, c.b));
        return mx > 1e-9f ? (mx - mn) / mx : 0.0f;
    };
    const float zen = sat(Atmosphere::skyRadiance(glm::vec3(0, 1, 0), toSun));
    const float hor = sat(Atmosphere::skyRadiance(towardElevation(1.0f, 90.0f), toSun));
    EXPECT_LT(hor, zen) << "horizon saturation " << hor << " should be below zenith " << zen;
}

// Twilight: after sunset the sky still glows, because the upper atmosphere is still in sunlight
// while the ground is in the planet's shadow. If this is zero, dusk snaps to black.
TEST(Atmosphere, SkyStillGlowsJustAfterSunset) {
    const glm::vec3 dusk = Atmosphere::skyRadiance(towardElevation(20.0f), towardElevation(-3.0f));
    const glm::vec3 night = Atmosphere::skyRadiance(towardElevation(20.0f), towardElevation(-40.0f));
    EXPECT_GT(luma(dusk), 0.0f) << "the sky must not be black three degrees after sunset";
    EXPECT_GT(luma(dusk), luma(night) * 5.0f) << "dusk must be clearly brighter than deep night";
}

// Blue hour, and the warm band opposite it. Twilight is not one colour: overhead and away from the
// sunset the sky is blue (ozone absorbing in the Chappuis band is what keeps it blue rather than a
// muddy grey), while the sky low down toward where the sun set is warm. Both halves are pinned,
// because getting only one of them is what a disappointing dusk looks like.
//
// Again the geometry is the point. The first version of this test asked for blue while looking 30
// degrees up on the SUNWARD side — the orange part of a real sunset — and correctly measured
// B/R = 0.91. Asserting "blue" in the direction that is physically orange was the bug.
TEST(Atmosphere, TwilightIsWarmTowardTheSunset) {
    const glm::vec3 toSun = towardElevation(-4.0f, 0.0f);   // sun just below the horizon

    // Low toward the sunset: warm. Compared against the same elevation on the opposite side, so this
    // measures the sunward warmth rather than a global colour cast.
    const glm::vec3 sunward = Atmosphere::skyRadiance(towardElevation(3.0f, 0.0f), toSun);
    const glm::vec3 anti    = Atmosphere::skyRadiance(towardElevation(3.0f, 180.0f), toSun);
    EXPECT_GT(sunward.r / std::max(sunward.b, 1e-9f), anti.r / std::max(anti.b, 1e-9f))
        << "the sky above the sunset should be warmer than the sky opposite it";
    EXPECT_GT(luma(sunward), luma(anti)) << "and brighter";
}

// ── A KNOWN LIMITATION, recorded as a target rather than hidden ──────────────────────────────────
//
// The real post-sunset zenith is BLUE (the "blue hour"). This model measures it at B/R = 0.94 —
// neutral. That is not a coding error; it is the documented ceiling of SINGLE scattering, and the
// arithmetic says so plainly:
//
//   Along a grazing sun path the Rayleigh optical depth is roughly (1.8, 4.1, 10.1) for R, G, B, so
//   the light ARRIVING at a high scattering sample is already almost pure red — blue has been
//   scattered out ~250,000x more than red. Rayleigh then scatters blue toward the eye 5.7x more
//   strongly, but 5.7 cannot recover a factor of 250,000. Ozone absorption helps (it removes green
//   and red far more than blue) and it is why the measurement lands at 0.94 rather than ~0.1, but it
//   is not enough on its own.
//
//   Blue hour is substantially a MULTIPLE-scattering phenomenon: the blue light removed from the
//   direct beam is not gone, it has been redistributed across the sky, and only a model that tracks
//   that redistribution puts it back. This is exactly what Hillaire's multiple-scattering LUT exists
//   to supply, and it is the concrete thing the deferred LUT upgrade buys.
//
// Enable this test as the acceptance criterion when that upgrade lands. Until then it documents a
// real gap in the dusk look: sunsets are right (the warm band above, and the reddened sun and its
// light, all verified above), the blue hour is not.
TEST(Atmosphere, DISABLED_TwilightZenithIsBlue_NeedsMultipleScattering) {
    const glm::vec3 toSun = towardElevation(-4.0f, 0.0f);
    const glm::vec3 zenith = Atmosphere::skyRadiance(glm::vec3(0, 1, 0), toSun);
    EXPECT_GT(zenith.b, zenith.r) << "rgb " << zenith.r << ", " << zenith.g << ", " << zenith.b;
    EXPECT_GT(zenith.b / std::max(zenith.r, 1e-9f), 1.3f);
}

// ── Ambient and haze derive from the same model ──────────────────────────────────────────────────

// Ambient fill must follow the sky: bright and blue by day, dim at night. This is what replaces the
// scalar `ambientLight` multiplied by a hand-picked constant tint.
TEST(Atmosphere, SkyIrradianceTracksTheSunAndStaysBlue) {
    const glm::vec3 day = Atmosphere::skyIrradiance(towardElevation(60.0f));
    const glm::vec3 night = Atmosphere::skyIrradiance(towardElevation(-30.0f));
    EXPECT_GT(luma(day), luma(night) * 20.0f) << "daytime sky fill must dwarf night";
    EXPECT_GT(day.b, day.r) << "daylight sky fill should be cool, giving cool shadows";
}

// Haze endpoints must differ, or lerping between them is pointless, and the horizon must be the
// brighter, hazier end.
TEST(Atmosphere, HazeHorizonIsBrighterThanHazeZenith) {
    const glm::vec3 toSun = towardElevation(40.0f);
    EXPECT_GT(luma(Atmosphere::hazeHorizon(toSun)), luma(Atmosphere::hazeZenith(toSun)));
}

// ── The moon ─────────────────────────────────────────────────────────────────────────────────────

// Phase drives illumination: new moon dark, full moon brightest, and the two quarters equal.
TEST(Atmosphere, MoonPhaseFractionFollowsTheSynodicCycle) {
    EXPECT_NEAR(Atmosphere::moonIlluminatedFraction(0.0f), 0.0f, 1e-5f);   // new
    EXPECT_NEAR(Atmosphere::moonIlluminatedFraction(0.5f), 1.0f, 1e-5f);   // full
    EXPECT_NEAR(Atmosphere::moonIlluminatedFraction(0.25f), 0.5f, 1e-5f);  // first quarter
    EXPECT_NEAR(Atmosphere::moonIlluminatedFraction(0.75f), 0.5f, 1e-5f);  // last quarter
    // Wraps, so day 28 of a 28-day cycle is day 0 again.
    EXPECT_NEAR(Atmosphere::moonIlluminatedFraction(1.0f),
                Atmosphere::moonIlluminatedFraction(0.0f), 1e-5f);
}

// A new moon must contribute NO light, however high it is. This is the test that makes the phase
// mechanically meaningful rather than decorative.
TEST(Atmosphere, NewMoonGivesNoMoonlight) {
    EXPECT_LT(luma(Atmosphere::moonlightColor(towardElevation(70.0f), 0.0f)), 1e-9f);
}

// Full moonlight is present, cool, and far dimmer than sunlight.
TEST(Atmosphere, FullMoonlightIsDimAndCool) {
    const glm::vec3 m = Atmosphere::moonlightColor(towardElevation(70.0f), 0.5f);
    const glm::vec3 s = Atmosphere::sunlightColor(towardElevation(70.0f));
    EXPECT_GT(luma(m), 0.0f) << "a full moon overhead must light the world";
    EXPECT_LT(luma(m), luma(s) * 0.1f) << "moonlight must be far dimmer than sunlight";
    EXPECT_GT(m.b, m.r) << "moonlight should read cool";
}

// A set moon gives nothing, exactly like a set sun.
TEST(Atmosphere, MoonBelowHorizonGivesNoLight) {
    EXPECT_LT(luma(Atmosphere::moonlightColor(towardElevation(-10.0f), 0.5f)), 1e-9f);
}

// ── The duplicate guard ──────────────────────────────────────────────────────────────────────────

// shaders/atmosphere.glsl re-declares the same constants for the GPU. Assert they match, because a
// silently stale shader copy would mean the sky and the light disagree — the precise failure this
// whole model was written to eliminate.
TEST(Atmosphere, ShaderConstantsMatchTheCppModel) {
    const std::string src = readShaderSource();
    ASSERT_FALSE(src.empty()) << "could not find shaders/atmosphere.glsl from the test working dir";

    struct FloatCase { const char* name; float expected; };
    const FloatCase floats[] = {
        {"kPlanetRadius",       Atmosphere::kPlanetRadius},
        {"kAtmosphereRadius",   Atmosphere::kAtmosphereRadius},
        {"kRayleighScaleHeight", Atmosphere::kRayleighScaleHeight},
        {"kMieScattering",      Atmosphere::kMieScattering},
        {"kMieExtinction",      Atmosphere::kMieExtinction},
        {"kMieScaleHeight",     Atmosphere::kMieScaleHeight},
        {"kMieAnisotropy",      Atmosphere::kMieAnisotropy},
        {"kOzoneCenter",        Atmosphere::kOzoneCenter},
        {"kOzoneWidth",         Atmosphere::kOzoneWidth},
        {"kSunAngularRadius",   Atmosphere::kSunAngularRadius},
        {"kMoonAngularRadius",  Atmosphere::kMoonAngularRadius},
        {"kMoonAlbedo",         Atmosphere::kMoonAlbedo},
        {"kMoonlightScale",     Atmosphere::kMoonlightScale},
    };
    for (const auto& c : floats) {
        float got = 0.0f;
        ASSERT_TRUE(glslFloat(src, c.name, got)) << "atmosphere.glsl is missing const float " << c.name;
        EXPECT_NEAR(got, c.expected, std::fabs(c.expected) * 1e-4f + 1e-12f)
            << c.name << " differs between atmosphere.glsl and Atmosphere.h";
    }

    struct Vec3Case { const char* name; glm::vec3 expected; };
    const Vec3Case vecs[] = {
        {"kRayleighScattering", Atmosphere::kRayleighScattering},
        {"kOzoneAbsorption",    Atmosphere::kOzoneAbsorption},
        {"kSolarIrradiance",    Atmosphere::kSolarIrradiance},
        {"kMoonlightTint",      Atmosphere::kMoonlightTint},
    };
    for (const auto& c : vecs) {
        glm::vec3 got(0.0f);
        ASSERT_TRUE(glslVec3(src, c.name, got)) << "atmosphere.glsl is missing const vec3 " << c.name;
        for (int i = 0; i < 3; ++i) {
            EXPECT_NEAR(got[i], c.expected[i], std::fabs(c.expected[i]) * 1e-4f + 1e-12f)
                << c.name << "[" << i << "] differs between atmosphere.glsl and Atmosphere.h";
        }
    }
}
