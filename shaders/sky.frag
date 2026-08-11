#version 450

// sky.frag — everything behind the geometry: the scattered sky, the sun's disc, and the moon's.
//
// This replaces a flat clear colour. Before this pass existed the scene was cleared to a single
// DayNightCycle-computed RGB, so there was no gradient, no horizon, no sun and no moon — the "sky"
// was one colour, and the sun was an invisible direction that shadows happened to point away from.
//
// The model lives in atmosphere.glsl and is shared with the CPU side that derives the scene's
// directional light and ambient fill, so the sun you see here and the light falling on the world
// come from the same transmittance. They cannot disagree.
//
// WHY IT IS SAFE TO PAY FOR THIS PER PIXEL (for now): the pass runs once, before geometry, with
// depth test and write off, so it costs one fullscreen evaluation of a 12-step view march with a
// 5-step inner sun march. That is measurable and, if it proves too expensive, the documented next
// step is a small sky-view LUT — which changes where phxSkyRadiance is evaluated, not what it
// computes. Measure before optimising.

#extension GL_GOOGLE_include_directive : require
#include "atmosphere.glsl"   // the shared scattering model (also compiled into the CPU light path)
#include "lighting.glsl"     // phxTonemap — the sky must go through the SAME curve as the world

layout(push_constant) uniform SkyPush {
    vec4 camRight;    // xyz = right * tan(fovX/2);  w = camera altitude in METRES
    vec4 camUp;       // xyz = up    * tan(fovY/2)
    vec4 camForward;  // xyz = forward (unit);       w = exposure
    vec4 toSun;       // xyz = unit direction TOWARD the sun;  w = tonemap curve (0 none, 1 AgX)
    vec4 toMoon;      // xyz = unit direction TOWARD the moon
} push;

layout(location = 0) in vec3 vRayDir;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 dir = normalize(vRayDir);
    float altitudeM = max(push.camRight.w, 1.0);
    vec3 radiance = phxAtmosphere(dir, push.toSun.xyz, push.toMoon.xyz, altitudeM);
    // The SAME exposure and curve the world uses. If the sky had its own the two would drift, and a
    // sky that disagrees with the ground it meets is the most obvious artifact there is.
    outColor = vec4(phxTonemap(radiance, push.camForward.w, int(push.toSun.w)), 1.0);
}
