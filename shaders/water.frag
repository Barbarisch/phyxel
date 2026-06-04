#version 450
//
// water.frag — Phase 1 stylized water surface shading.
//
// A blue depth-tinted body, a sky-colored Fresnel rim at grazing angles, animated
// ripple normals, a sun glint, and — when reflectionEnabled — a planar reflection
// sampled from the reflection texture (the scene re-rendered from the mirrored
// camera across the sea plane). Refraction/depth-based color/foam come in Phase 1b.
//
layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D reflectionTex;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = quad size, zw unused
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w unused
} pc;

// Cheap animated surface normal from two crossing directional sine waves.
vec3 waterNormal(vec2 p, float t) {
    vec2 d1 = normalize(vec2( 1.0, 0.4));
    vec2 d2 = normalize(vec2(-0.6, 1.0));
    float a1 = 0.06, a2 = 0.04;     // amplitudes
    float f1 = 0.35, f2 = 0.6;      // spatial frequencies
    float s1 = 0.9,  s2 = 1.3;      // temporal speeds

    float phase1 = dot(p, d1) * f1 + t * s1;
    float phase2 = dot(p, d2) * f2 + t * s2;

    float dx = a1 * f1 * d1.x * cos(phase1) + a2 * f2 * d2.x * cos(phase2);
    float dz = a1 * f1 * d1.y * cos(phase1) + a2 * f2 * d2.y * cos(phase2);
    return normalize(vec3(-dx, 1.0, -dz));
}

void main() {
    vec3  camPos = pc.camPosTime.xyz;
    float t      = pc.camPosTime.w;

    vec3 N = waterNormal(fragWorldPos.xz, t);
    vec3 V = normalize(camPos - fragWorldPos);

    // Fresnel: ~0 looking straight down (see into the water), ~1 at grazing angles.
    float ndv  = clamp(dot(V, N), 0.0, 1.0);
    float fres = clamp(0.04 + 0.96 * pow(1.0 - ndv, 5.0), 0.0, 1.0);

    // Stylized palette.
    vec3 deepColor    = vec3(0.02, 0.10, 0.18);
    vec3 shallowColor = vec3(0.10, 0.35, 0.45);
    vec3 skyColor     = vec3(0.55, 0.72, 0.92);

    vec3 baseColor = mix(shallowColor, deepColor, ndv);

    // Reflection color: a procedural sky gradient + reflected sun, sampled along the
    // view direction reflected about the (rippled) surface normal. Cheap, correct, and
    // free of the planar-reflection artifacts. (Planar scene reflection is deferred —
    // the dormant branch below re-enables it once a correct reflection pass exists.)
    vec3 sunDir = normalize(vec3(0.4, 0.85, 0.35)); // direction TO the sun
    vec3 R = reflect(-V, N);                          // reflected view ray

    vec3  horizonCol = vec3(0.72, 0.82, 0.95);
    vec3  zenithCol  = vec3(0.24, 0.46, 0.80);
    float up         = clamp(R.y, 0.0, 1.0);
    vec3  sky        = mix(horizonCol, zenithCol, pow(up, 0.6));

    float sunDisc = pow(max(dot(R, sunDir), 0.0), 900.0);  // tight bright disc
    float sunGlow = pow(max(dot(R, sunDir), 0.0), 40.0);   // soft halo
    vec3  sunCol  = vec3(1.0, 0.96, 0.86);

    vec3 reflColor = sky + sunCol * (sunDisc * 2.0 + sunGlow * 0.12);

    // Dormant: planar scene reflection (re-enable once a correct reflection pass lands).
    if (pc.params2.z > 0.5) {
        vec2 screenUV = gl_FragCoord.xy / pc.params2.xy;
        screenUV += N.xz * 0.03;
        screenUV = clamp(screenUV, vec2(0.001), vec2(0.999));
        reflColor = mix(reflColor, texture(reflectionTex, screenUV).rgb, 0.85);
    }

    vec3 color = mix(baseColor, reflColor, fres);

    // A touch of direct sun glitter, always visible (additive, not gated by Fresnel).
    color += sunCol * sunDisc * 0.5;

    float alpha = clamp(0.55 + 0.4 * fres, 0.0, 0.97);
    outColor = vec4(color, alpha);
}
