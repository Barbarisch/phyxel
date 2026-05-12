#version 450
// transparent_voxel.frag — Weighted Blended OIT for transparent voxels (e.g. Glass).
//
// Outputs to two render targets:
//   location 0: accum  (RGBA16F) — weighted color accumulation
//   location 1: reveal (R8_UNORM) — product of (1 - alpha) across all layers
//
// Composite equation (in post_process.frag):
//   finalColor = mix(accum.rgb / accum.a, opaqueColor, reveal)

#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 shadowCoord;
layout(location = 3) in flat uint flags;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inWorldPos;

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

// MRT outputs
layout(location = 0) out vec4 accumColor;  // OIT accumulation
layout(location = 1) out float revealFactor; // OIT reveal (1 - alpha)

vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    uint safeIdx = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= atlasUVs.textureCount)
        safeIdx = atlasUVs.fallbackIndex;
    vec4 bounds = atlasUVs.textureUVs[safeIdx];
    return mix(bounds.xy, bounds.zw, localUV);
}

float calcAttenuation(float d, float radius) {
    float linear    = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten     = 1.0 / (1.0 + linear * d + quadratic * d * d);
    float falloff   = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624,  -0.39906216), vec2( 0.94558609, -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2( 0.34495938,  0.29387760),
    vec2(-0.91588581,   0.45771432), vec2(-0.81544232, -0.87912464),
    vec2(-0.38277543,   0.27676845), vec2( 0.97484398,  0.75648379),
    vec2( 0.44323325,  -0.97511554), vec2( 0.53742981, -0.47373420),
    vec2(-0.26496911,  -0.41893023), vec2( 0.79197514,  0.19090188),
    vec2(-0.24188840,   0.99706507), vec2(-0.81409955,  0.91437590),
    vec2( 0.19984126,   0.78641367), vec2( 0.14383161, -0.14100790)
);

void main() {
    // OIT is temporarily disabled: transparent voxels now render in the opaque pass
    // (voxel.frag). Re-enable when the bloom pipeline is wired up to fix the UNDEFINED
    // layout validation error that corrupts the post-process composite.
    discard;

    // --- code below preserved for when OIT is re-enabled ---
    // Only process transparent voxels (bit 1 of flags); skip mirror voxels
    if ((flags & 2u) == 0u) discard;
    if ((flags & (1u << 10u)) != 0u) discard;

    vec2 atlasUV = getAtlasUV(textureIndex, texCoord);
    vec4 textureColor = texture(textureAtlas, atlasUV);

    if (textureColor.a < 0.01) discard;

    float matAlpha = float((flags >> 2u) & 0xFFu) / 255.0;
    float alpha = max(textureColor.a, max(matAlpha, 0.01));

    // Lighting (same as voxel.frag)
    vec3 normal   = normalize(inNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);
    vec3 viewDir  = normalize(ubo.cameraPosition - inWorldPos);

    float diff = max(dot(normal, lightDir), 0.0);
    float sunSpec = 0.0;
    if (diff > 0.0) {
        vec3 halfVec = normalize(lightDir + viewDir);
        sunSpec = pow(max(dot(normal, halfVec), 0.0), 64.0) * 0.3;
    }

    // PCF shadow
    float shadowFactor = 1.0;
    if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0.0) {
        float shadowSum = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
        for (int i = 0; i < 16; i++) {
            float pcfDepth = texture(shadowMap, shadowCoord.xy + poissonDisk[i] * texelSize * 1.5).r;
            if (shadowCoord.z - 0.005 > pcfDepth) shadowSum += 1.0;
        }
        shadowFactor = 1.0 - (shadowSum / 16.0);
    }

    vec3 ambient = vec3(ubo.ambientLight);
    vec3 sunContrib = (diff * ubo.sunColor + sunSpec * ubo.sunColor) * shadowFactor;
    vec3 finalLight = ambient + sunContrib;

    // Point lights
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius  = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;
        vec3 toLight = lightPos - inWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            float atten = calcAttenuation(dist, radius);
            vec3 h = normalize(ldir + viewDir);
            float pSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
            finalLight += lightColor * intensity * (ndotl + pSpec) * atten;
        }
    }

    vec3 litColor = textureColor.rgb * finalLight;

    // WBOIT weight: higher weight for closer, more opaque fragments
    // Use linear z (view-space) for better weight distribution
    float z = -(ubo.view * vec4(inWorldPos, 1.0)).z;
    float weight = alpha * clamp(0.03 / (1e-5 + pow(z / 200.0, 4.0)), 0.01, 3000.0);

    accumColor   = vec4(litColor * alpha * weight, alpha * weight);
    revealFactor = 1.0 - alpha;
}
