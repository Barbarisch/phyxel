#pragma once
// WaterCore 31 (M4): the water's push on a body - the pressure integral over its oriented box (docs/WaterCore.md 31.2).
// The arithmetic is the shader's own: shaders/water_body_force.glsl, compiled here as C++ over glm, so the unit tests
// (tests/core/WaterBodyForceTest.cpp) check exactly what solver_integrate runs.
#include <cmath>
#include <functional>
#include <limits>
#include <glm/glm.hpp>

namespace Phyxel::Core::Water::BodyForce {
using namespace glm;
/// The water the function samples (the shader's wbfP): world point -> (kinematic pressure, the y that pressure belongs to);
/// p <= 0 air, NaN unknown. Set per call by accel() below.
inline thread_local std::function<vec2(vec3)> t_pressure;
inline vec2 wbfP(vec3 x) { return t_pressure(x); }
#include "water_body_force.glsl"

/// The acceleration (b = 1) on a box (centre c, half extents he along unit axes a0..a2) in the water `p`; w = submerged fraction,
/// -1 = a sample was unknown.
inline vec4 accel(const std::function<vec2(vec3)>& p, vec3 c, vec3 he, vec3 a0, vec3 a1, vec3 a2, float h, float g,
                  float levelCap = std::numeric_limits<float>::infinity()) {
    t_pressure = p;
    const vec4 r = wbfAccel(c, he, a0, a1, a2, h, g, levelCap);
    t_pressure = nullptr;
    return r;
}
}  // namespace Phyxel::Core::Water::BodyForce
