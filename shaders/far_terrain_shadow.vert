#version 450

// FAR-CASCADE shadow caster for far-terrain LOD tiles (depth-only twin of far_terrain.vert —
// identical placement math, ONLY the projection swaps to the far cascade's light matrix; the
// grass_shadow.vert discipline). The mid map ends at 420 u, so without this pass the LOD band
// received no terrain self-shadowing at all (hills could not shade their own valleys).

layout(location = 0) in vec3 inPos;      // tile-local x/z, absolute world y
layout(location = 1) in uint inPacked;   // unused here (depth only)

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
    mat4 lightSpaceMatrixFar;   // THE projection for this pass (camera-relative in/out)
} ubo;

layout(push_constant) uniform PushConstants {
    vec2 tileOriginRel;  // (tile min corner - camera).xz, double-subtracted on CPU
    vec2 tileOriginAbs;  // unused here; kept so the CPU push struct stays shared
} pc;

void main() {
    vec3 rp = vec3(pc.tileOriginRel.x + inPos.x,
                   inPos.y - ubo.cameraWorld.y,
                   pc.tileOriginRel.y + inPos.z);
    gl_Position = ubo.lightSpaceMatrixFar * vec4(rp, 1.0);
}
