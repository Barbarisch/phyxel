#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader
layout(location = 2) in vec4 shadowCoord;        // from vertex shader

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
    
    // Shadow calculation
    float shadow = 1.0;
    if (shadowCoord.z > -1.0 && shadowCoord.z < 1.0) {
        float dist = texture(shadowMap, shadowCoord.xy).r;
        if (shadowCoord.w > 0.0 && dist < shadowCoord.z - 0.005) {
            shadow = 0.5; // In shadow
        }
    }

    // Apply shadow
    outColor = vec4(textureColor.rgb * shadow, textureColor.a);
    
    // Fallback to solid color if texture is transparent or invalid
    if (outColor.a < 0.1) {
        // Use a placeholder color based on texture index for debugging
        float hue = float(textureIndex % 6u) / 6.0;
        outColor = vec4(hue, 0.5, 1.0, 1.0);  // HSV-like color
    }
}
