#version 450
#extension GL_GOOGLE_include_directive : require

// sky.frag — everything behind the geometry: the scattered sky and every celestial body.
//
// This replaces a flat clear colour. Before this pass the scene was cleared to a single RGB, so
// there was no gradient, no horizon, no sun and no moon.
//
// The model lives in atmosphere.glsl and is shared with the CPU side that derives the scene's
// directional light and ambient fill, so the sun you see here and the light falling on the world
// come from the same transmittance.
//
// BODIES COME FROM THE UBO, not push constants. A push block is 128 bytes and each body needs
// three vec4s, so an array of them does not fit — which is why this pass binds descriptor set 0
// even though it needs nothing else from it. The camera basis stays in push constants because it
// changes per draw, not per frame.

#include "atmosphere.glsl"   // the shared scattering model (also compiled into the CPU light path)
#include "lighting.glsl"     // phxTonemap — the sky must go through the SAME curve as the world

// std140 PREFIX of the shared UBO. Declared only as far as the fields this shader reads; every
// field before them must still be listed so the offsets line up.
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
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;
    mat4 biasedLightSpace;
    vec3 cameraWorld;
    int  debugShadowMode;
    float shadowDepthRange;
    vec4  grassDisplacers[16];
    vec4  grassDisplacersAux[16];
    ivec4 grassDisplacerMeta;
    mat4 biasedLightSpaceNear;
    vec4 shadowCascadeNear;
    mat4 lightSpaceMatrixNear;
    mat4 biasedLightSpaceFar;
    vec4 shadowCascadeFar;
    mat4 lightSpaceMatrixFar;
    vec3 ambientColor;
    vec3 hazeHorizonColor;
    vec3 hazeZenithColor;
    vec3 moonDirection;
    vec3 moonColor;
    float exposure;
    int   tonemapCurve;
    // Celestial bodies (graphics/CelestialBody.h).
    vec4 skyBodyDirRadius[4];
    vec4 skyBodyDisc[4];
    vec4 skyBodyLitDir[4];
    vec4 skyBodyLight[4];
    int  skyBodyCount;
} ubo;

layout(push_constant) uniform SkyPush {
    vec4 camRight;    // xyz = right * tan(fovX/2);  w = camera altitude in METRES
    vec4 camUp;       // xyz = up    * tan(fovY/2)
    vec4 camForward;  // xyz = forward (unit)
    vec4 toSun;       // xyz = unit direction TOWARD the primary star (drives the sky's scattering)
    vec4 toMoon;      // legacy slot, unused now that bodies come from the UBO
} push;

layout(location = 0) in vec3 vRayDir;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 dir = normalize(vRayDir);
    float altitudeM = max(push.camRight.w, 1.0);

    vec3 radiance = phxAtmosphereBodies(dir, push.toSun.xyz, altitudeM,
                                        ubo.skyBodyDirRadius, ubo.skyBodyDisc,
                                        ubo.skyBodyLitDir, ubo.skyBodyCount);

    // The SAME exposure and curve the world uses. A sky with its own would drift from the ground it
    // meets, and that seam is the most visible artifact available.
    outColor = vec4(phxTonemap(radiance, ubo.exposure, ubo.tonemapCurve), 1.0);
}
