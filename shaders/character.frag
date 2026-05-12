#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

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

    vec3 ambient = vec3(ubo.ambientLight);
    vec3 finalLight = ambient + (diff + sunSpec) * ubo.sunColor;

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
