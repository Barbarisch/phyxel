#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 3) in vec4 fragBakedLight; // x = skylight (0..1), yzw = block RGB (0..1)

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

// Sun shadow map (shared set-0 binding 2, same as voxel.frag). Characters now RECEIVE
// sun shadows so a character standing in a building's shadow is darkened like the world.
layout(set = 0, binding = 2) uniform sampler2D shadowMap;

const vec2 poissonDisk[16] = vec2[](
    vec2(-0.94201624,  -0.39906216), vec2( 0.94558609,  -0.76890725),
    vec2(-0.094184101, -0.92938870), vec2( 0.34495938,   0.29387760),
    vec2(-0.91588581,   0.45771432), vec2(-0.81544232,  -0.87912464),
    vec2(-0.38277543,   0.27676845), vec2( 0.97484398,   0.75648379),
    vec2( 0.44323325,  -0.97511554), vec2( 0.53742981,  -0.47373420),
    vec2(-0.26496911,  -0.41893023), vec2( 0.79197514,   0.19090188),
    vec2(-0.24188840,   0.99706507), vec2(-0.81409955,   0.91437590),
    vec2( 0.19984126,   0.78641367), vec2( 0.14383161,  -0.14100790)
);

layout(location = 0) out vec4 outColor;

float calcAttenuation(float d, float radius) {
    float linear = 4.5 / radius;
    float quadratic = 75.0 / (radius * radius);
    float atten = 1.0 / (1.0 + linear * d + quadratic * d * d);
    float falloff = clamp(1.0 - (d / radius), 0.0, 1.0);
    return atten * falloff;
}

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);
    vec3 viewDir = normalize(ubo.cameraPosition - fragWorldPos);
    float diff = max(dot(normal, lightDir), 0.0);

    // Blinn-Phong specular (sun)
    float sunSpec = 0.0;
    if (diff > 0.0) {
        vec3 halfVec = normalize(lightDir + viewDir);
        sunSpec = pow(max(dot(normal, halfVec), 0.0), 32.0) * 0.3;
    }

    // Baked light field (Phase 4): characters react to the same skylight + block light
    // as the world, so they darken in sealed rooms and pick up glow/spell light. Mirrors
    // voxel.frag: sky is a FILL (kSkyFill), sun is the KEY gated by skylight, block adds on top.
    float sky        = fragBakedLight.x;
    vec3  blockColor = fragBakedLight.yzw;
    float skyCurve   = sky * sky;
    const float kSkyFill = 0.35;

    // Sun shadow: compute the light-space coord in the frag (the vert has no UBO) and PCF-sample
    // the same shadow map the world uses. biasMat maps clip xy→[0,1]; z stays [0,1] (orthoRH_ZO).
    const mat4 biasMat = mat4(0.5, 0.0, 0.0, 0.0,
                              0.0, 0.5, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.5, 0.5, 0.0, 1.0);
    vec4 shadowCoord = biasMat * ubo.lightSpaceMatrix * vec4(fragWorldPos, 1.0);
    float shadowFactor = 1.0;
    bool inShadowMap = shadowCoord.x >= 0.0 && shadowCoord.x <= 1.0 &&
                       shadowCoord.y >= 0.0 && shadowCoord.y <= 1.0;
    if (inShadowMap && shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0.0) {
        float shadowSum = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
        const float kShadowBias = 0.0009; // slightly larger than world's 0.0006 (chars aren't greedy-merged casters)
        for (int i = 0; i < 16; i++) {
            float pcfDepth = texture(shadowMap, shadowCoord.xy + poissonDisk[i] * texelSize * 1.5).r;
            if (shadowCoord.z - kShadowBias > pcfDepth) shadowSum += 1.0;
        }
        shadowFactor = 1.0 - (shadowSum / 16.0);
    }

    vec3 ambient = vec3(ubo.ambientLight) * skyCurve * kSkyFill;
    vec3 finalLight = ambient + (diff + sunSpec) * ubo.sunColor * skyCurve * shadowFactor;
    finalLight += blockColor * blockColor; // omnidirectional warm/colored fill from baked block light

    // Point lights
    for (uint i = 0u; i < lights.numPointLights && i < 32u; i++) {
        vec3 lightPos = lights.pointLights[i].positionAndRadius.xyz;
        float radius = lights.pointLights[i].positionAndRadius.w;
        vec3 lightColor = lights.pointLights[i].colorAndIntensity.xyz;
        float intensity = lights.pointLights[i].colorAndIntensity.w;

        vec3 toLight = lightPos - fragWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            float atten = calcAttenuation(dist, radius);
            float pSpec = 0.0;
            if (ndotl > 0.0) {
                vec3 h = normalize(ldir + viewDir);
                pSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
            }
            finalLight += lightColor * intensity * (ndotl + pSpec) * atten;
        }
    }

    // Spot lights
    for (uint i = 0u; i < lights.numSpotLights && i < 16u; i++) {
        vec3 lightPos = lights.spotLights[i].positionAndRadius.xyz;
        float radius = lights.spotLights[i].positionAndRadius.w;
        vec3 spotDir = normalize(lights.spotLights[i].directionAndInnerCone.xyz);
        float innerCone = lights.spotLights[i].directionAndInnerCone.w;
        vec3 lightColor = lights.spotLights[i].colorAndIntensity.xyz;
        float intensity = lights.spotLights[i].colorAndIntensity.w;
        float outerCone = lights.spotLights[i].outerConeAndPadding.x;

        vec3 toLight = lightPos - fragWorldPos;
        float dist = length(toLight);
        if (dist < radius) {
            vec3 ldir = toLight / dist;
            float ndotl = max(dot(normal, ldir), 0.0);
            float atten = calcAttenuation(dist, radius);
            float theta = dot(-ldir, spotDir);
            float spotFactor = smoothstep(outerCone, innerCone, theta);
            float sSpec = 0.0;
            if (ndotl > 0.0) {
                vec3 h = normalize(ldir + viewDir);
                sSpec = pow(max(dot(normal, h), 0.0), 32.0) * 0.3;
            }
            finalLight += lightColor * intensity * (ndotl + sSpec) * atten * spotFactor;
        }
    }

    outColor = vec4(fragColor * finalLight, 1.0);
}
