// Auto-generated GLSL texture atlas header for Phyxel Engine
// Atlas size: 256x256
// Texture count: 72

// Atlas constants - these match our generated cube_atlas.h
const uint ATLAS_SIZE = 256u;           // Atlas dimensions
const uint TEXTURE_SIZE = 18u;          // Individual texture size
const uint PADDING = 1u;                // Padding between textures
const uint TEXTURES_PER_ROW = 6u;       // Calculated from atlas layout

// Pre-calculated UV coordinates for each texture (matches cube_atlas.h exactly)
const vec4 TEXTURE_UVS[72] = vec4[72](
    vec4(0.003906, 0.003906, 0.074219, 0.074219),  // 0: placeholder_side_n
    vec4(0.082031, 0.003906, 0.152344, 0.074219),  // 1: placeholder_side_s
    vec4(0.160156, 0.003906, 0.230469, 0.074219),  // 2: placeholder_side_e
    vec4(0.238281, 0.003906, 0.308594, 0.074219),  // 3: placeholder_side_w
    vec4(0.316406, 0.003906, 0.386719, 0.074219),  // 4: placeholder_top
    vec4(0.394531, 0.003906, 0.464844, 0.074219),  // 5: placeholder_bottom
    vec4(0.003906, 0.082031, 0.074219, 0.152344),  // 6: grassdirt_side_n
    vec4(0.082031, 0.082031, 0.152344, 0.152344),  // 7: grassdirt_side_s
    vec4(0.160156, 0.082031, 0.230469, 0.152344),  // 8: grassdirt_side_e
    vec4(0.238281, 0.082031, 0.308594, 0.152344),  // 9: grassdirt_side_w
    vec4(0.316406, 0.082031, 0.386719, 0.152344),  // 10: grassdirt_top
    vec4(0.394531, 0.082031, 0.464844, 0.152344),  // 11: grassdirt_bottom
    vec4(0.003906, 0.160156, 0.074219, 0.230469),  // 12: cork_side_n
    vec4(0.082031, 0.160156, 0.152344, 0.230469),  // 13: default_side_n
    vec4(0.160156, 0.160156, 0.230469, 0.230469),  // 14: glass_side_n
    vec4(0.238281, 0.160156, 0.308594, 0.230469),  // 15: glow_side_n
    vec4(0.316406, 0.160156, 0.386719, 0.230469),  // 16: hover_side_n
    vec4(0.394531, 0.160156, 0.464844, 0.230469),  // 17: ice_side_n
    vec4(0.003906, 0.238281, 0.074219, 0.308594),  // 18: metal_side_n
    vec4(0.082031, 0.238281, 0.152344, 0.308594),  // 19: rubber_side_n
    vec4(0.160156, 0.238281, 0.230469, 0.308594),  // 20: stone_side_n
    vec4(0.238281, 0.238281, 0.308594, 0.308594),  // 21: wood_side_n
    vec4(0.316406, 0.238281, 0.386719, 0.308594),  // 22: cork_side_s
    vec4(0.394531, 0.238281, 0.464844, 0.308594),  // 23: default_side_s
    vec4(0.003906, 0.316406, 0.074219, 0.386719),  // 24: glass_side_s
    vec4(0.082031, 0.316406, 0.152344, 0.386719),  // 25: glow_side_s
    vec4(0.160156, 0.316406, 0.230469, 0.386719),  // 26: hover_side_s
    vec4(0.238281, 0.316406, 0.308594, 0.386719),  // 27: ice_side_s
    vec4(0.316406, 0.316406, 0.386719, 0.386719),  // 28: metal_side_s
    vec4(0.394531, 0.316406, 0.464844, 0.386719),  // 29: rubber_side_s
    vec4(0.003906, 0.394531, 0.074219, 0.464844),  // 30: stone_side_s
    vec4(0.082031, 0.394531, 0.152344, 0.464844),  // 31: wood_side_s
    vec4(0.160156, 0.394531, 0.230469, 0.464844),  // 32: cork_side_e
    vec4(0.238281, 0.394531, 0.308594, 0.464844),  // 33: default_side_e
    vec4(0.316406, 0.394531, 0.386719, 0.464844),  // 34: glass_side_e
    vec4(0.394531, 0.394531, 0.464844, 0.464844),  // 35: glow_side_e
    vec4(0.003906, 0.472656, 0.074219, 0.542969),  // 36: hover_side_e
    vec4(0.082031, 0.472656, 0.152344, 0.542969),  // 37: ice_side_e
    vec4(0.160156, 0.472656, 0.230469, 0.542969),  // 38: metal_side_e
    vec4(0.238281, 0.472656, 0.308594, 0.542969),  // 39: rubber_side_e
    vec4(0.316406, 0.472656, 0.386719, 0.542969),  // 40: stone_side_e
    vec4(0.394531, 0.472656, 0.464844, 0.542969),  // 41: wood_side_e
    vec4(0.003906, 0.550781, 0.074219, 0.621094),  // 42: cork_side_w
    vec4(0.082031, 0.550781, 0.152344, 0.621094),  // 43: default_side_w
    vec4(0.160156, 0.550781, 0.230469, 0.621094),  // 44: glass_side_w
    vec4(0.238281, 0.550781, 0.308594, 0.621094),  // 45: glow_side_w
    vec4(0.316406, 0.550781, 0.386719, 0.621094),  // 46: hover_side_w
    vec4(0.394531, 0.550781, 0.464844, 0.621094),  // 47: ice_side_w
    vec4(0.003906, 0.628906, 0.074219, 0.699219),  // 48: metal_side_w
    vec4(0.082031, 0.628906, 0.152344, 0.699219),  // 49: rubber_side_w
    vec4(0.160156, 0.628906, 0.230469, 0.699219),  // 50: stone_side_w
    vec4(0.238281, 0.628906, 0.308594, 0.699219),  // 51: wood_side_w
    vec4(0.316406, 0.628906, 0.386719, 0.699219),  // 52: cork_top
    vec4(0.394531, 0.628906, 0.464844, 0.699219),  // 53: default_top
    vec4(0.003906, 0.707031, 0.074219, 0.777344),  // 54: glass_top
    vec4(0.082031, 0.707031, 0.152344, 0.777344),  // 55: glow_top
    vec4(0.160156, 0.707031, 0.230469, 0.777344),  // 56: hover_top
    vec4(0.238281, 0.707031, 0.308594, 0.777344),  // 57: ice_top
    vec4(0.316406, 0.707031, 0.386719, 0.777344),  // 58: metal_top
    vec4(0.394531, 0.707031, 0.464844, 0.777344),  // 59: rubber_top
    vec4(0.003906, 0.785156, 0.074219, 0.855469),  // 60: stone_top
    vec4(0.082031, 0.785156, 0.152344, 0.855469),  // 61: wood_top
    vec4(0.160156, 0.785156, 0.230469, 0.855469),  // 62: cork_bottom
    vec4(0.238281, 0.785156, 0.308594, 0.855469),  // 63: default_bottom
    vec4(0.316406, 0.785156, 0.386719, 0.855469),  // 64: glass_bottom
    vec4(0.394531, 0.785156, 0.464844, 0.855469),  // 65: glow_bottom
    vec4(0.003906, 0.863281, 0.074219, 0.933594),  // 66: hover_bottom
    vec4(0.082031, 0.863281, 0.152344, 0.933594),  // 67: ice_bottom
    vec4(0.160156, 0.863281, 0.230469, 0.933594),  // 68: metal_bottom
    vec4(0.238281, 0.863281, 0.308594, 0.933594),  // 69: rubber_bottom
    vec4(0.316406, 0.863281, 0.386719, 0.933594),  // 70: stone_bottom
    vec4(0.394531, 0.863281, 0.464844, 0.933594)  // 71: wood_bottom
);

// Calculate atlas UV coordinates from texture index and local UV
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    // Handle invalid/placeholder texture
    uint safeTexIndex = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= 72u) {  // Invalid texture index
        safeTexIndex = 6u;      // Use placeholder_bottom texture
    }
    
    // Get pre-calculated UV bounds for this texture
    vec4 uvBounds = TEXTURE_UVS[safeTexIndex];
    vec2 uvMin = uvBounds.xy;
    vec2 uvMax = uvBounds.zw;
    
    // Interpolate between min and max based on local UV
    return mix(uvMin, uvMax, localUV);
}
