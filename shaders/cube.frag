#version 450

layout(location = 0) in flat uint textureIndex;  // from vertex shader
layout(location = 1) in vec2 texCoord;           // from vertex shader

layout(set = 0, binding = 1) uniform sampler2D textureAtlas;  // texture atlas sampler

layout(location = 0) out vec4 outColor;   // output color

// Atlas constants - these match our generated cube_atlas.h
const uint ATLAS_SIZE = 128u;           // Atlas dimensions
const uint TEXTURE_SIZE = 18u;          // Individual texture size
const uint PADDING = 2u;                // Padding between textures
const uint TEXTURES_PER_ROW = 5u;       // Calculated from atlas layout

// Pre-calculated UV coordinates for each texture (matches cube_atlas.h exactly)
const vec4 TEXTURE_UVS[12] = vec4[12](
    vec4(0.015625, 0.015625, 0.156250, 0.156250),  // 0: grassdirt_bottom
    vec4(0.187500, 0.015625, 0.328125, 0.156250),  // 1: grassdirt_side_e
    vec4(0.359375, 0.015625, 0.500000, 0.156250),  // 2: grassdirt_side_n
    vec4(0.531250, 0.015625, 0.671875, 0.156250),  // 3: grassdirt_side_s
    vec4(0.703125, 0.015625, 0.843750, 0.156250),  // 4: grassdirt_side_w
    vec4(0.015625, 0.187500, 0.156250, 0.328125),  // 5: grassdirt_top
    vec4(0.187500, 0.187500, 0.328125, 0.328125),  // 6: placeholder_bottom
    vec4(0.359375, 0.187500, 0.500000, 0.328125),  // 7: placeholder_side_e
    vec4(0.531250, 0.187500, 0.671875, 0.328125),  // 8: placeholder_side_n
    vec4(0.703125, 0.187500, 0.843750, 0.328125),  // 9: placeholder_side_s
    vec4(0.015625, 0.359375, 0.156250, 0.500000),  // 10: placeholder_side_w
    vec4(0.187500, 0.359375, 0.328125, 0.500000)   // 11: placeholder_top
);

// Calculate atlas UV coordinates from texture index and local UV
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    // Handle invalid/placeholder texture
    uint safeTexIndex = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= 12u) {  // Invalid texture index
        safeTexIndex = 6u;      // Use placeholder_bottom texture
    }
    
    // Get pre-calculated UV bounds for this texture
    vec4 uvBounds = TEXTURE_UVS[safeTexIndex];
    vec2 uvMin = uvBounds.xy;
    vec2 uvMax = uvBounds.zw;
    
    // Interpolate between min and max based on local UV
    return mix(uvMin, uvMax, localUV);
}

void main() {
    // Calculate atlas UV coordinates
    vec2 atlasUV = getAtlasUV(textureIndex, texCoord);
    
    // Sample from texture atlas
    vec4 textureColor = texture(textureAtlas, atlasUV);
    
    // For now, output the texture color directly
    // Later we can add lighting, tinting, etc.
    outColor = textureColor;
    
    // Fallback to solid color if texture is transparent or invalid
    if (outColor.a < 0.1) {
        // Use a placeholder color based on texture index for debugging
        float hue = float(textureIndex % 6u) / 6.0;
        outColor = vec4(hue, 0.5, 1.0, 1.0);  // HSV-like color
    }
}
