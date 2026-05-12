#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 shadowCoord;
layout(location = 3) in flat uint flags;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inWorldPos;

// Main descriptor set (set 0) — same as opaque voxel shader but with reflectedViewProj
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
    mat4 reflectedViewProj; // Reflected camera VP matrix for projective sampling
} ubo;

layout(set = 0, binding = 1) uniform sampler2D textureAtlas;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;

struct PointLightGPU {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
};
struct SpotLightGPU {
    vec4 positionAndRadius;
    vec4 directionAndInnerCone;
    vec4 colorAndIntensity;
    vec4 outerConeAndPadding;
};
layout(std430, set = 0, binding = 3) readonly buffer LightBuffer {
    uint numPointLights;
    uint numSpotLights;
    uint _pad0;
    uint _pad1;
    PointLightGPU pointLights[32];
    SpotLightGPU spotLights[16];
} lights;

layout(std430, set = 0, binding = 4) readonly buffer AtlasUVBuffer {
    uint textureCount;
    uint fallbackIndex;
    uint _pad0;
    uint _pad1;
    vec4 textureUVs[];
} atlasUVs;

// Reflection texture at set 1, binding 0
layout(set = 1, binding = 0) uniform sampler2D reflectionSampler;

layout(location = 0) out vec4 outColor;

void main() {
    // Only render mirror faces (bit 10 of flags)
    if ((flags & (1u << 10u)) == 0u) discard;

    // Projective texturing: compute UV into the reflection render target
    // reflectedViewProj transforms world positions into the reflected camera's clip space
    vec4 projPos = ubo.reflectedViewProj * vec4(inWorldPos, 1.0);
    vec2 projUV = projPos.xy / projPos.w;
    projUV = projUV * 0.5 + 0.5;
    projUV.y = 1.0 - projUV.y;  // Vulkan NDC Y is flipped relative to texture UV

    // Clamp to avoid border artifacts
    projUV = clamp(projUV, vec2(0.001), vec2(0.999));

    // Sample the reflection image
    vec4 reflColor = texture(reflectionSampler, projUV);

    // Fresnel: reflection strength increases at grazing angles
    vec3 viewDir = normalize(ubo.cameraPosition - inWorldPos);
    vec3 N = normalize(inNormal);
    float cosTheta = max(dot(viewDir, N), 0.0);
    float fresnel = 0.05 + 0.95 * pow(1.0 - cosTheta, 3.0);
    fresnel = clamp(fresnel, 0.1, 1.0);

    // Slight darkening at the surface edges for depth
    vec3 mirrorColor = reflColor.rgb * mix(0.85, 1.0, fresnel);

    outColor = vec4(mirrorColor, 1.0);
}
