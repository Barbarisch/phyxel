#version 450
//
// water_underwater.frag — the view from BELOW the surface (WaterSystemV3 Phase 1, item 5).
//
// A fullscreen overlay drawn at the end of the water pass whenever the camera is submerged. It
// reads the scene depth buffer (already bound read-only for the water pass) and fogs the scene by
// distance, so underwater the world fades into the water's own colour instead of looking exactly
// like being in air. Depth test is OFF: the fog must also cover the sky and the underside of the
// surface, which are the most obvious "I am underwater" cues.
//
// APPROXIMATION (deliberate, stated): true underwater extinction is per-channel (red dies first),
// which a single alpha-blended overlay cannot express exactly. Instead the fog COLOUR carries the
// blue-green tint and shifts further toward blue with distance, and a single alpha carries the
// density. Getting exact per-channel transmittance would need the scene colour as an input texture
// (a second full-res copy) — not worth it for a stylized engine.
//
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Shared scene UBO — std140 PREFIX (only the fields we use, in order).
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D refractionTex;
layout(set = 1, binding = 1) uniform sampler2D sceneDepthTex;
layout(set = 1, binding = 2) uniform sampler2D reflectionTex;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 camPosTime; // xyz = camera world position, w = time (seconds)
    vec4 params;     // x = seaLevel, y = quad size, z = submergence 0..1, w = depth below surface
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w = turbidity
} pc;

// Same derivation as water_common.glsl: linear distance along the camera's forward axis from a
// depth-buffer value, taken from the projection matrix so it is clip-convention independent.
float linearDepth(float d, mat4 P) {
    return P[3][2] / (d + P[2][2]);
}

void main() {
    float submergence = clamp(pc.params.z, 0.0, 1.0);
    if (submergence <= 0.0) discard;

    float d = texture(sceneDepthTex, inUV).r;
    float dist = linearDepth(d, ubo.proj);
    // The far plane (nothing drawn) reads as an enormous distance — clamp so open water/sky
    // saturates the fog rather than producing inf/NaN.
    dist = clamp(dist, 0.0, 400.0);

    // Underwater visibility, now PER BODY (v4 W2). It used to be one constant, so diving into a
    // murky pond looked exactly like diving into open ocean — and once the SURFACE varies by body,
    // a fixed underwater fog makes breaking the surface pop.
    //
    // VIS_CLEAR is the shipped 22.0, UNCHANGED — turbidity 0 must keep today's look exactly. It was
    // always a legibility-tuned figure rather than a measured one, and it stays that way.
    //
    // ⚑VIS_TURBID is derived from the clear value by a GROUNDED RATIO rather than picked by eye, so
    // the absolute scale inherits the existing legibility tuning while the relative change is real:
    //   * clear coastal water Z_SD ~ 20 m, Kd ~ 1.7/Z_SD = 0.085 /m
    //     (Poole, H.H. & Atkins, W.R.G. 1929, J. Mar. Biol. Assoc. UK 16(1):297-324)
    //   * eutrophic lake Z_SD ~ 1.5 m (Carlson 1977 TSI, Limnol. Oceanogr. 22(2):361-368),
    //     Kd ~ 1.4/Z_SD = 0.93 /m (Holmes 1970 — the turbid-estuary form of the same relation)
    //   => turbid water attenuates ~11x faster, so 22.0 / 11 = 2.0.
    // An earlier 3.0 here was a guess; this replaces it. (Note VISIBILITY is a 1/e fog distance, NOT
    // a Secchi depth — only the RATIO transfers between the two quantities, which is why the ratio
    // is what is used.)
    const float VIS_CLEAR  = 22.0;
    const float VIS_TURBID = 2.0;
    float turbidity = clamp(pc.params2.w, 0.0, 1.0);
    float VISIBILITY = mix(VIS_CLEAR, VIS_TURBID, turbidity);
    float fog = 1.0 - exp(-dist / VISIBILITY);

    // Depth below the surface darkens and blues the water — sunlight is absorbed on the way down.
    float depthBelow = max(pc.params.w, 0.0);
    float sunk = clamp(depthBelow / 30.0, 0.0, 1.0);

    vec3 shallowFog = vec3(0.09, 0.30, 0.36);   // sunlit blue-green
    vec3 deepFog    = vec3(0.01, 0.05, 0.11);   // the dark blue of depth
    vec3 fogCol = mix(shallowFog, deepFog, sunk);

    // Light the fog with the LIVE sun + ambient so submerged water goes black at night, matching
    // the surface shading in water_common.glsl.
    vec3  toSun = normalize(-ubo.sunDirection);
    float daylight = clamp(toSun.y * 1.5 + 0.15, 0.0, 1.0);
    fogCol *= mix(vec3(0.15), ubo.sunColor * 1.1, daylight) * max(ubo.ambientLight, 0.25);

    // Distance also shifts the remaining light toward blue (red is gone first).
    fogCol = mix(fogCol, fogCol * vec3(0.55, 0.85, 1.25), fog * 0.6);

    outColor = vec4(fogCol, fog * submergence);
}
