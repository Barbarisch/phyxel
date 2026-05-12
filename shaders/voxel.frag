#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader
layout(location = 3) in flat uint flags;         // from vertex shader
layout(location = 4) in vec3 inNormal;           // from vertex shader
layout(location = 5) in vec3 inWorldPos;         // from vertex shader

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

layout(set = 0, binding = 1) uniform sampler2D textureAtlas;  // texture atlas sampler
layout(set = 0, binding = 2) uniform sampler2D shadowMap;     // shadow map sampler

// Point light (32 bytes, std430)
struct PointLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 colorAndIntensity;     // xyz = color, w = intensity
};

// Spot light (64 bytes, std430)
struct SpotLightGPU {
    vec4 positionAndRadius;     // xyz = position, w = radius
    vec4 directionAndInnerCone; // xyz = direction, w = innerCone
    vec4 colorAndIntensity;     // xyz = color, w = intensity
    vec4 outerConeAndPadding;   // x = outerCone, yzw = padding
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

layout(location = 0) out vec4 outColor;   // output color

// Get atlas UV coordinates from texture index and local UV
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    uint safeIdx = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= atlasUVs.textureCount) {
        safeIdx = atlasUVs.fallbackIndex;
    }
    vec4 bounds = atlasUVs.textureUVs[safeIdx];
    return mix(bounds.xy, bounds.zw, localUV);
}

// Calculate attenuation for a light at distance d with given radius
float calcAttenuation(float d, float radius) {
    float linear = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten = 1.0 / (1.0 + linear * d + quadratic * d * d);
    // Smooth cutoff at radius
    float falloff = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

// 16-sample Poisson disk for soft shadow PCF
const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624,  -0.39906216),
    vec2( 0.94558609,  -0.76890725),
    vec2(-0.094184101, -0.92938870),
    vec2( 0.34495938,   0.29387760),
    vec2(-0.91588581,   0.45771432),
    vec2(-0.81544232,  -0.87912464),
    vec2(-0.38277543,   0.27676845),
    vec2( 0.97484398,   0.75648379),
    vec2( 0.44323325,  -0.97511554),
    vec2( 0.53742981,  -0.47373420),
    vec2(-0.26496911,  -0.41893023),
    vec2( 0.79197514,   0.19090188),
    vec2(-0.24188840,   0.99706507),
    vec2(-0.81409955,   0.91437590),
    vec2( 0.19984126,   0.78641367),
    vec2( 0.14383161,  -0.14100790)
);

void main() {
    // Calculate atlas UV coordinates
    vec2 atlasUV = getAtlasUV(textureIndex, texCoord);
    
    // Sample from texture atlas
    vec4 textureColor = texture(textureAtlas, atlasUV);

    // Discard fully transparent fragments (cutout transparency)
    if (textureColor.a < 0.1) discard;

    // Discard mirror fragments — handled in the mirror pass
    if ((flags & (1u << 10u)) != 0u) discard;

    // NOTE: transparent voxels (flags & 2u) render here in the opaque pass (same as
    // kinematic/dynamic glass). OIT is not used until the bloom pipeline is also wired up.

    // Check for emissive flag (bit 0)
    bool isEmissive = (flags & 1u) != 0u;

    // Normal and Light Direction
    vec3 normal = normalize(inNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);
    vec3 viewDir = normalize(ubo.cameraPosition - inWorldPos);

    // Diffuse lighting (sun)
    float diff = max(dot(normal, lightDir), 0.0);

    // Blinn-Phong specular (sun)
    float sunSpec = 0.0;
    if (diff > 0.0) {
        vec3 halfVec = normalize(lightDir + viewDir);
        sunSpec = pow(max(dot(normal, halfVec), 0.0), 32.0) * 0.3;
    }

    // Shadow calculation — 16-sample Poisson disk PCF
    float shadowFactor = 1.0;
    if (!isEmissive && shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0.0) {
        float shadowSum = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
        for (int i = 0; i < 16; i++) {
            float pcfDepth = texture(shadowMap, shadowCoord.xy + poissonDisk[i] * texelSize * 1.5).r;
            if (shadowCoord.z - 0.005 > pcfDepth) {
                shadowSum += 1.0;
            }
        }
        shadowFactor = 1.0 - (shadowSum / 16.0);
    }

    // Apply shadow (or boost if emissive)
    if (isEmissive) {
        outColor = vec4(textureColor.rgb * ubo.emissiveMultiplier, textureColor.a); // Boost brightness for bloom
    } else {
        // Combine Ambient + (Diffuse + Specular) * Shadow
        vec3 ambient = vec3(ubo.ambientLight);
        vec3 sunContrib = (diff * ubo.sunColor + sunSpec * ubo.sunColor) * shadowFactor;
        
        vec3 finalLight = ambient + sunContrib;

        // Accumulate point light contributions
        for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
            vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
            float radius = lights.pointLights[i].positionAndRadius.w;
            vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
            float intensity = lights.pointLights[i].colorAndIntensity.w;

            vec3 toLight = lightPos - inWorldPos;
            float dist = length(toLight);
            if (dist < radius) {
                vec3 ldir = toLight / dist;
                float ndotl = max(dot(normal, ldir), 0.0);
                float atten = calcAttenuation(dist, radius);
                // Point light specular
                float pSpec = 0.0;
                if (ndotl > 0.0) {
                    vec3 h = normalize(ldir + viewDir);
                    pSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
                }
                finalLight += lightColor * intensity * (ndotl + pSpec) * atten;
            }
        }

        // Accumulate spot light contributions
        for (uint i = 0u; i < lights.numSpotLights && i < 16u; i++) {
            vec3 lightPos = lights.spotLights[i].positionAndRadius.xyz;
            float radius = lights.spotLights[i].positionAndRadius.w;
            vec3 spotDir = normalize(lights.spotLights[i].directionAndInnerCone.xyz);
            float innerCone = lights.spotLights[i].directionAndInnerCone.w;
            vec3 lightColor = lights.spotLights[i].colorAndIntensity.xyz;
            float intensity = lights.spotLights[i].colorAndIntensity.w;
            float outerCone = lights.spotLights[i].outerConeAndPadding.x;

            vec3 toLight = lightPos - inWorldPos;
            float dist = length(toLight);
            if (dist < radius) {
                vec3 ldir = toLight / dist;
                float ndotl = max(dot(normal, ldir), 0.0);
                float atten = calcAttenuation(dist, radius);
                // Spotlight cone falloff
                float theta = dot(-ldir, spotDir);
                float spotFactor = smoothstep(outerCone, innerCone, theta);
                // Spot light specular
                float sSpec = 0.0;
                if (ndotl > 0.0) {
                    vec3 h = normalize(ldir + viewDir);
                    sSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
                }
                finalLight += lightColor * intensity * (ndotl + sSpec) * atten * spotFactor;
            }
        }
        
        outColor = vec4(textureColor.rgb * finalLight, textureColor.a);
    }
}
