// CelestialBodyTest.cpp — does the data-driven sky behave like a sky?
//
// The point of the body list is that "two moons" or "a binary sun" becomes configuration rather than
// code. These tests pin the properties that has to preserve: the default two-body sky must behave
// exactly like the hardcoded sun+moon it replaces, phases must fall out of GEOMETRY rather than a
// parameter, and only one body may ever own the shadow cascades.

#include <gtest/gtest.h>

#include "graphics/CelestialBody.h"

#include <cmath>

using namespace Phyxel::Graphics;

namespace {
float luma(const glm::vec3& c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }
float elevationDeg(const glm::vec3& d) { return glm::degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f))); }
}  // namespace

// ── The default sky must not change how the engine looks ─────────────────────────────────────────

TEST(CelestialBody, DefaultSkyIsASunAndAMoon) {
    const SkyBodies s = SkyBodies::defaultSky();
    ASSERT_EQ(s.bodies.size(), 2u);
    EXPECT_TRUE(s.bodies[0].emissive) << "the first body should be the star";
    EXPECT_FALSE(s.bodies[1].emissive) << "the second should be lit by it";
    EXPECT_EQ(s.bodies[1].litBy, 0);
}

// The sun must be overhead at noon and below the horizon at midnight — the single most basic thing a
// sky can get wrong, and the thing a refactor of the orbit math would break first.
TEST(CelestialBody, SunIsUpAtNoonAndDownAtMidnight) {
    SkyBodies s = SkyBodies::defaultSky();

    s.update(12.0f, 0);
    EXPECT_GT(elevationDeg(s.directions[0]), 80.0f) << "the sun should be near the zenith at noon";
    EXPECT_GT(luma(s.lightColors[0]), 0.5f) << "and delivering strong light";

    s.update(0.0f, 0);
    EXPECT_LT(elevationDeg(s.directions[0]), -80.0f) << "and well below the horizon at midnight";
    EXPECT_LT(luma(s.lightColors[0]), 1e-6f) << "delivering none";
}

// ── Phases come from geometry, not a parameter ───────────────────────────────────────────────────

// Over a lunar cycle the moon must pass through new and full, and the illuminated fraction must be
// the angle to its light source — never a number someone set.
TEST(CelestialBody, MoonPhaseFollowsItsAngleToTheSun) {
    SkyBodies s = SkyBodies::defaultSky();
    float minLit = 2.0f, maxLit = -1.0f;
    for (int day = 0; day < 28; ++day) {
        s.update(0.0f, day);
        const float lit = s.litFractions[1];
        // The defining relationship: lit fraction IS (1 - cos(separation)) / 2.
        const float cosSep = glm::dot(s.directions[1], s.directions[0]);
        EXPECT_NEAR(lit, 0.5f * (1.0f - cosSep), 1e-4f) << "day " << day;
        minLit = std::min(minLit, lit);
        maxLit = std::max(maxLit, lit);
    }
    EXPECT_LT(minLit, 0.05f) << "the cycle should reach new moon";
    EXPECT_GT(maxLit, 0.95f) << "and full moon";
}

// A new moon delivers nothing however high it sits. This is what makes the phase mechanically real
// rather than decorative.
TEST(CelestialBody, NewMoonGivesNoLight) {
    SkyBodies s = SkyBodies::defaultSky();
    // Day 0 midnight: the moon shares the sun's start, so it is new.
    s.update(0.0f, 0);
    ASSERT_LT(s.litFractions[1], 0.05f) << "test premise: this should be a new moon";
    EXPECT_LT(luma(s.lightColors[1]), 1e-7f);
}

// ── Configuration, not code: the cases the whole refactor exists for ─────────────────────────────

// TWO MOONS on different orbits must sit in different places. If plane tilt and period did nothing,
// extra bodies would just stack on top of each other and the feature would be pointless.
TEST(CelestialBody, TwoMoonsOnDifferentOrbitsSeparate) {
    SkyBodies s = SkyBodies::defaultSky();
    CelestialBody second = s.bodies[1];
    second.name = "second_moon";
    second.periodDays = 0.6f;          // a fast inner moon
    second.phaseOffset = 0.3f;
    second.planeTilt = 0.5f;           // ~29 degrees out of the sun's plane
    s.bodies.push_back(second);

    bool everApart = false;
    for (int day = 0; day < 8; ++day) {
        for (float h : {0.0f, 6.0f, 12.0f, 18.0f}) {
            s.update(h, day);
            const float cosSep = glm::dot(s.directions[1], s.directions[2]);
            if (cosSep < 0.9f) everApart = true;   // more than ~26 degrees apart
        }
    }
    EXPECT_TRUE(everApart) << "two moons with different periods, offsets and tilts never separated — "
                              "the orbit parameters are not doing anything";
}

// A SECOND SUN must light the world, which is the thing that would look wrong if extra bodies were
// visual-only.
TEST(CelestialBody, SecondSunContributesLight) {
    SkyBodies s = SkyBodies::defaultSky();
    CelestialBody companion;
    companion.name = "companion";
    companion.emissive = true;
    companion.periodDays = 1.0f;
    companion.phaseOffset = 0.15f;     // trails the primary, so both are up together for a while
    companion.tint = glm::vec3(1.0f, 0.7f, 0.5f);
    s.bodies.push_back(companion);

    s.update(12.0f, 0);
    ASSERT_GT(elevationDeg(s.directions[2]), 0.0f) << "test premise: the companion is above the horizon";
    EXPECT_GT(luma(s.lightColors[2]), 0.0f) << "a second sun above the horizon must light the world";
    EXPECT_GT(s.lightColors[2].r, s.lightColors[2].b) << "and carry its own tint";
}

// castsLight=false is the escape hatch for a purely decorative body.
TEST(CelestialBody, DecorativeBodyGivesNoLight) {
    SkyBodies s = SkyBodies::defaultSky();
    CelestialBody deco;
    deco.emissive = true;
    deco.castsLight = false;
    deco.phaseOffset = 0.15f;
    s.bodies.push_back(deco);
    s.update(12.0f, 0);
    EXPECT_LT(luma(s.lightColors[2]), 1e-9f);
}

// ── Only one body owns the shadow cascades ───────────────────────────────────────────────────────

// The cascades are fitted to a single direction, so the dominant-light rule is load-bearing: the
// brightest body currently UP wins, and it must be the sun by day and the moon by night.
TEST(CelestialBody, DominantLightIsTheSunByDayAndTheMoonAtNight) {
    SkyBodies s = SkyBodies::defaultSky();

    s.update(12.0f, 0);
    EXPECT_EQ(s.dominantLightIndex(), 0) << "the sun should own the cascades at noon";

    // Day 14 puts the moon opposite the sun (full), so at midnight it is high and the sun is not.
    s.update(0.0f, 14);
    const int dom = s.dominantLightIndex();
    ASSERT_GE(dom, 0) << "a full moon at midnight should be lighting the world";
    EXPECT_EQ(dom, 1) << "the moon should own the cascades when the sun is down";
}

// With nothing above the horizon there is no shadow caster, and the caller must be told so rather
// than handed a body that would fit the cascades to a light below the ground.
TEST(CelestialBody, NoDominantLightOnAMoonlessNight) {
    SkyBodies s = SkyBodies::defaultSky();
    s.update(0.0f, 0);   // new moon at midnight: sun down, moon down and dark
    EXPECT_EQ(s.dominantLightIndex(), -1);
}

// A body below the horizon may never be chosen, whatever its nominal brightness.
TEST(CelestialBody, DominantLightIgnoresBodiesBelowTheHorizon) {
    SkyBodies s = SkyBodies::defaultSky();
    s.update(0.0f, 14);
    const int dom = s.dominantLightIndex();
    if (dom >= 0) {
        EXPECT_GT(s.directions[dom].y, 0.0f) << "the chosen shadow caster is below the horizon";
    }
}
