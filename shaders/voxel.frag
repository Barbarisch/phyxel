#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader
layout(location = 3) in flat uint flags;         // from vertex shader
layout(location = 4) in vec3 inNormal;           // from vertex shader

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
} ubo;

layout(set = 0, binding = 1) uniform sampler2D textureAtlas;  // texture atlas sampler
layout(set = 0, binding = 2) uniform sampler2D shadowMap;     // shadow map sampler

layout(location = 0) out vec4 outColor;   // output color

// Include auto-generated texture atlas constants
#include "../resources/textures/cube_atlas.glsl"

void main() {
    // Calculate atlas UV coordinates
    vec2 atlasUV = getAtlasUV(textureIndex, texCoord);
    
    // Sample from texture atlas
    vec4 textureColor = texture(textureAtlas, atlasUV);
    
    // Check for emissive flag (bit 0)
    bool isEmissive = (flags & 1u) != 0u;

    // Normal and Light Direction
    vec3 normal = normalize(inNormal);
    vec3 lightDir = normalize(-ubo.sunDirection);

    // Diffuse lighting
    float diff = max(dot(normal, lightDir), 0.0);

    // Shadow calculation (PCF)
    float shadowFactor = 1.0;
    if (!isEmissive && shadowCoord.z > -1.0 && shadowCoord.z < 1.0 && shadowCoord.w > 0.0) {
        float shadowSum = 0.0;
        vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
        for(int x = -1; x <= 1; ++x) {
            for(int y = -1; y <= 1; ++y) {
                float pcfDepth = texture(shadowMap, shadowCoord.xy + vec2(x, y) * texelSize).r; 
                if (shadowCoord.z - 0.005 > pcfDepth) {
                    shadowSum += 1.0;
                }
            }    
        }
        shadowFactor = 1.0 - (shadowSum / 9.0);
    }

    // Apply shadow (or boost if emissive)
    if (isEmissive) {
        outColor = vec4(textureColor.rgb * ubo.emissiveMultiplier, textureColor.a); // Boost brightness for bloom
    } else {
        // Combine Ambient + Diffuse * Shadow
        vec3 ambient = vec3(ubo.ambientLight);
        vec3 diffuse = diff * ubo.sunColor;
        
        vec3 finalLight = ambient + (diffuse * shadowFactor);
        
        outColor = vec4(textureColor.rgb * finalLight, textureColor.a);
    }
    
    // Fallback to solid color if texture is transparent or invalid
    if (outColor.a < 0.1) {
        // Use a placeholder color based on texture index for debugging
        float hue = float(textureIndex % 6u) / 6.0;
        outColor = vec4(hue, 0.5, 1.0, 1.0);  // HSV-like color
    }
}
