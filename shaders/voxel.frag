#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader
layout(location = 3) in flat uint flags;         // from vertex shader

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    uint numInstances;
    float ambientLight;
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

    // Shadow calculation
    float shadow = 1.0;
    if (!isEmissive && shadowCoord.z > -1.0 && shadowCoord.z < 1.0) {
        float dist = texture(shadowMap, shadowCoord.xy).r;
        if (shadowCoord.w > 0.0 && dist < shadowCoord.z - 0.005) {
            shadow = 0.5; // In shadow
        }
    }

    // Apply shadow (or boost if emissive)
    if (isEmissive) {
        outColor = vec4(textureColor.rgb * 2.0, textureColor.a); // Boost brightness for bloom
    } else {
        // Apply ambient light scaling
        // shadow is 1.0 (lit) or 0.5 (shadowed)
        // ubo.ambientLight acts as a global brightness multiplier
        float lightIntensity = shadow * ubo.ambientLight;
        outColor = vec4(textureColor.rgb * lightIntensity, textureColor.a);
    }
    
    // Fallback to solid color if texture is transparent or invalid
    if (outColor.a < 0.1) {
        // Use a placeholder color based on texture index for debugging
        float hue = float(textureIndex % 6u) / 6.0;
        outColor = vec4(hue, 0.5, 1.0, 1.0);  // HSV-like color
    }
}
