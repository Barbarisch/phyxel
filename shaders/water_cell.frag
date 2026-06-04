#version 450
//
// water_cell.frag — per-cell water surface shading.
//
// Same stylized procedural look as the flat sea surface (water.frag): blue
// depth-tinted body, sky-gradient + reflected-sun Fresnel reflection, animated
// ripple normals. No reflection texture / refraction yet.
//
layout(location = 0) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
} pc;

vec3 waterNormal(vec2 p, float t) {
    vec2 d1 = normalize(vec2( 1.0, 0.4));
    vec2 d2 = normalize(vec2(-0.6, 1.0));
    float a1 = 0.06, a2 = 0.04, f1 = 0.35, f2 = 0.6, s1 = 0.9, s2 = 1.3;
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

    float ndv  = clamp(dot(V, N), 0.0, 1.0);
    float fres = clamp(0.04 + 0.96 * pow(1.0 - ndv, 5.0), 0.0, 1.0);

    vec3 deepColor    = vec3(0.02, 0.10, 0.18);
    vec3 shallowColor = vec3(0.10, 0.35, 0.45);
    vec3 baseColor    = mix(shallowColor, deepColor, ndv);

    vec3 sunDir = normalize(vec3(0.4, 0.85, 0.35));
    vec3 R = reflect(-V, N);
    vec3 horizonCol = vec3(0.72, 0.82, 0.95);
    vec3 zenithCol  = vec3(0.24, 0.46, 0.80);
    vec3 sky = mix(horizonCol, zenithCol, pow(clamp(R.y, 0.0, 1.0), 0.6));
    float sunDisc = pow(max(dot(R, sunDir), 0.0), 900.0);
    float sunGlow = pow(max(dot(R, sunDir), 0.0), 40.0);
    vec3  sunCol  = vec3(1.0, 0.96, 0.86);
    vec3 reflColor = sky + sunCol * (sunDisc * 2.0 + sunGlow * 0.12);

    vec3 color = mix(baseColor, reflColor, fres);
    color += sunCol * sunDisc * 0.5;

    float alpha = clamp(0.6 + 0.35 * fres, 0.0, 0.97);
    outColor = vec4(color, alpha);
}
