#version 450

// FAR-CASCADE shadow caster for instanced far-tree LOD meshes (depth-only twin of
// far_tree_mesh.vert — identical instance placement, projection swapped to the far cascade's
// light matrix). This is what makes DISTANT FORESTS cast shadows onto the far terrain: before
// it, everything past the mid map's 420 u rendered flat-lit. Fades/level partitions are
// deliberately NOT replicated: a shadow a kilometre out being a frame ahead of a dithering
// tree is invisible at 0.9 u/texel, and skipping the discards keeps this pass raster-cheap.

layout(location = 0) in vec3 inPos;
layout(location = 1) in uint inPacked;      // unused (depth only)

layout(location = 2) in vec4 inInstPosH;    // localX, worldY (trunk base, abs), localZ, height
layout(location = 3) in float inInstCanopyR;
layout(location = 4) in uint inInstPacked;

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
    mat4 lightSpaceMatrixFar;   // THE projection for this pass
} ubo;

layout(push_constant) uniform PushConstants {
    vec2 tileOriginRel;
    vec2 tileOriginAbs;
    vec2 fadeIn;
    float baseHeight;
    float minFade;
    vec2 levelBand;   // same layout as far_tree_mesh.vert's push (shared CPU struct)
} pc;

void main() {
    vec3 base = vec3(pc.tileOriginRel.x + inInstPosH.x,
                     inInstPosH.y - ubo.cameraWorld.y,
                     pc.tileOriginRel.y + inInstPosH.z);
    gl_Position = ubo.lightSpaceMatrixFar * vec4(base + inPos, 1.0);
}
