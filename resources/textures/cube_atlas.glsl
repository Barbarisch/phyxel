// Auto-generated GLSL texture atlas header for Phyxel Engine
// Atlas size: 128x128
// Texture count: 12

// Atlas constants - these match our generated cube_atlas.h
const uint ATLAS_SIZE = 128u;           // Atlas dimensions
const uint TEXTURE_SIZE = 18u;          // Individual texture size
const uint PADDING = 1u;                // Padding between textures
const uint TEXTURES_PER_ROW = 6u;       // Calculated from atlas layout

// Pre-calculated UV coordinates for each texture (matches cube_atlas.h exactly)
const vec4 TEXTURE_UVS[12] = vec4[12](
    vec4(0.007812, 0.007812, 0.148438, 0.148438),  // 0: grassdirt_bottom
    vec4(0.164062, 0.007812, 0.304688, 0.148438),  // 1: grassdirt_side_e
    vec4(0.320312, 0.007812, 0.460938, 0.148438),  // 2: grassdirt_side_n
    vec4(0.476562, 0.007812, 0.617188, 0.148438),  // 3: grassdirt_side_s
    vec4(0.632812, 0.007812, 0.773438, 0.148438),  // 4: grassdirt_side_w
    vec4(0.789062, 0.007812, 0.929688, 0.148438),  // 5: grassdirt_top
    vec4(0.007812, 0.164062, 0.148438, 0.304688),  // 6: placeholder_bottom
    vec4(0.164062, 0.164062, 0.304688, 0.304688),  // 7: placeholder_side_e
    vec4(0.320312, 0.164062, 0.460938, 0.304688),  // 8: placeholder_side_n
    vec4(0.476562, 0.164062, 0.617188, 0.304688),  // 9: placeholder_side_s
    vec4(0.632812, 0.164062, 0.773438, 0.304688),  // 10: placeholder_side_w
    vec4(0.789062, 0.164062, 0.929688, 0.304688)  // 11: placeholder_top
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
