#version 450
//
// water.frag — Phase 0 stylized water surface shading.
//
// Procedural look only (no textures, no reflection/refraction sampling yet — those
// are Phase 1): a blue depth-tinted body, a sky-colored Fresnel rim at grazing
// angles, animated ripple normals, and a sharp sun glint. Alpha-blended.
//
layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = quad size, zw unused
} pc;

// Cheap animated surface normal from two crossing directional sine waves.
// Returns a perturbed normal around (0,1,0); analytic slope keeps it smooth.
vec3 waterNormal(vec2 p, float t) {
    vec2 d1 = normalize(vec2( 1.0, 0.4));
    vec2 d2 = normalize(vec2(-0.6, 1.0));
    float a1 = 0.06, a2 = 0.04;     // amplitudes
    float f1 = 0.35, f2 = 0.6;      // spatial frequencies
    float s1 = 0.9,  s2 = 1.3;      // temporal speeds

    float phase1 = dot(p, d1) * f1 + t * s1;
    float phase2 = dot(p, d2) * f2 + t * s2;

    // d(height)/dx, d(height)/dz of the summed sines.
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
    vec3 color     = mix(baseColor, skyColor, fres);

    // Sun specular glint (light dir points toward the sun).
    vec3  L    = normalize(vec3(0.4, 0.9, 0.3));
    vec3  H    = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 220.0);
    color += vec3(1.0) * spec * 0.8;

    float alpha = clamp(0.55 + 0.4 * fres, 0.0, 0.95);
    outColor = vec4(color, alpha);
}
