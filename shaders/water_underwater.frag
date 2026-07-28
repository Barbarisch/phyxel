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
    vec4 params2;    // x = screen width, y = screen height, z = reflectionEnabled, w unused
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

    // Underwater visibility. ⚑GROUND: ~22 m to near-total extinction is the clear-coastal-water
    // range a diver actually sees; murkier than open ocean (~50 m), far clearer than a silty lake
    // (~3 m). Tuned for gameplay legibility rather than a specific water type.
    const float VISIBILITY = 22.0;
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
