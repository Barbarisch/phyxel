// Auto-generated GLSL texture atlas header for Phyxel Engine
// Atlas size: 512x512
// Texture count: 78

// Atlas constants - these match our generated cube_atlas.h
const uint ATLAS_SIZE = 512u;           // Atlas dimensions
const uint TEXTURE_SIZE = 18u;          // Individual texture size
const uint PADDING = 1u;                // Padding between textures
const uint TEXTURES_PER_ROW = 6u;       // Calculated from atlas layout

// Pre-calculated UV coordinates for each texture (matches cube_atlas.h exactly)
const vec4 TEXTURE_UVS[78] = vec4[78](
    vec4(0.001953, 0.001953, 0.037109, 0.037109),  // 0: cork_bottom
    vec4(0.041016, 0.001953, 0.076172, 0.037109),  // 1: cork_side_e
    vec4(0.080078, 0.001953, 0.115234, 0.037109),  // 2: cork_side_n
    vec4(0.119141, 0.001953, 0.154297, 0.037109),  // 3: cork_side_s
    vec4(0.158203, 0.001953, 0.193359, 0.037109),  // 4: cork_side_w
    vec4(0.197266, 0.001953, 0.232422, 0.037109),  // 5: cork_top
    vec4(0.001953, 0.041016, 0.037109, 0.076172),  // 6: default_bottom
    vec4(0.041016, 0.041016, 0.076172, 0.076172),  // 7: default_side_e
    vec4(0.080078, 0.041016, 0.115234, 0.076172),  // 8: default_side_n
    vec4(0.119141, 0.041016, 0.154297, 0.076172),  // 9: default_side_s
    vec4(0.158203, 0.041016, 0.193359, 0.076172),  // 10: default_side_w
    vec4(0.197266, 0.041016, 0.232422, 0.076172),  // 11: default_top
    vec4(0.001953, 0.080078, 0.037109, 0.115234),  // 12: glass_bottom
    vec4(0.041016, 0.080078, 0.076172, 0.115234),  // 13: glass_side_e
    vec4(0.080078, 0.080078, 0.115234, 0.115234),  // 14: glass_side_n
    vec4(0.119141, 0.080078, 0.154297, 0.115234),  // 15: glass_side_s
    vec4(0.158203, 0.080078, 0.193359, 0.115234),  // 16: glass_side_w
    vec4(0.197266, 0.080078, 0.232422, 0.115234),  // 17: glass_top
    vec4(0.001953, 0.119141, 0.037109, 0.154297),  // 18: glow_bottom
    vec4(0.041016, 0.119141, 0.076172, 0.154297),  // 19: glow_side_e
    vec4(0.080078, 0.119141, 0.115234, 0.154297),  // 20: glow_side_n
    vec4(0.119141, 0.119141, 0.154297, 0.154297),  // 21: glow_side_s
    vec4(0.158203, 0.119141, 0.193359, 0.154297),  // 22: glow_side_w
    vec4(0.197266, 0.119141, 0.232422, 0.154297),  // 23: glow_top
    vec4(0.001953, 0.158203, 0.037109, 0.193359),  // 24: grassdirt_bottom
    vec4(0.041016, 0.158203, 0.076172, 0.193359),  // 25: grassdirt_side_e
    vec4(0.080078, 0.158203, 0.115234, 0.193359),  // 26: grassdirt_side_n
    vec4(0.119141, 0.158203, 0.154297, 0.193359),  // 27: grassdirt_side_s
    vec4(0.158203, 0.158203, 0.193359, 0.193359),  // 28: grassdirt_side_w
    vec4(0.197266, 0.158203, 0.232422, 0.193359),  // 29: grassdirt_top
    vec4(0.001953, 0.197266, 0.037109, 0.232422),  // 30: hover_bottom
    vec4(0.041016, 0.197266, 0.076172, 0.232422),  // 31: hover_side_e
    vec4(0.080078, 0.197266, 0.115234, 0.232422),  // 32: hover_side_n
    vec4(0.119141, 0.197266, 0.154297, 0.232422),  // 33: hover_side_s
    vec4(0.158203, 0.197266, 0.193359, 0.232422),  // 34: hover_side_w
    vec4(0.197266, 0.197266, 0.232422, 0.232422),  // 35: hover_top
    vec4(0.001953, 0.236328, 0.037109, 0.271484),  // 36: ice_bottom
    vec4(0.041016, 0.236328, 0.076172, 0.271484),  // 37: ice_side_e
    vec4(0.080078, 0.236328, 0.115234, 0.271484),  // 38: ice_side_n
    vec4(0.119141, 0.236328, 0.154297, 0.271484),  // 39: ice_side_s
    vec4(0.158203, 0.236328, 0.193359, 0.271484),  // 40: ice_side_w
    vec4(0.197266, 0.236328, 0.232422, 0.271484),  // 41: ice_top
    vec4(0.001953, 0.275391, 0.037109, 0.310547),  // 42: leaf_bottom
    vec4(0.041016, 0.275391, 0.076172, 0.310547),  // 43: leaf_side_e
    vec4(0.080078, 0.275391, 0.115234, 0.310547),  // 44: leaf_side_n
    vec4(0.119141, 0.275391, 0.154297, 0.310547),  // 45: leaf_side_s
    vec4(0.158203, 0.275391, 0.193359, 0.310547),  // 46: leaf_side_w
    vec4(0.197266, 0.275391, 0.232422, 0.310547),  // 47: leaf_top
    vec4(0.001953, 0.314453, 0.037109, 0.349609),  // 48: metal_bottom
    vec4(0.041016, 0.314453, 0.076172, 0.349609),  // 49: metal_side_e
    vec4(0.080078, 0.314453, 0.115234, 0.349609),  // 50: metal_side_n
    vec4(0.119141, 0.314453, 0.154297, 0.349609),  // 51: metal_side_s
    vec4(0.158203, 0.314453, 0.193359, 0.349609),  // 52: metal_side_w
    vec4(0.197266, 0.314453, 0.232422, 0.349609),  // 53: metal_top
    vec4(0.001953, 0.353516, 0.037109, 0.388672),  // 54: placeholder_bottom
    vec4(0.041016, 0.353516, 0.076172, 0.388672),  // 55: placeholder_side_e
    vec4(0.080078, 0.353516, 0.115234, 0.388672),  // 56: placeholder_side_n
    vec4(0.119141, 0.353516, 0.154297, 0.388672),  // 57: placeholder_side_s
    vec4(0.158203, 0.353516, 0.193359, 0.388672),  // 58: placeholder_side_w
    vec4(0.197266, 0.353516, 0.232422, 0.388672),  // 59: placeholder_top
    vec4(0.001953, 0.392578, 0.037109, 0.427734),  // 60: rubber_bottom
    vec4(0.041016, 0.392578, 0.076172, 0.427734),  // 61: rubber_side_e
    vec4(0.080078, 0.392578, 0.115234, 0.427734),  // 62: rubber_side_n
    vec4(0.119141, 0.392578, 0.154297, 0.427734),  // 63: rubber_side_s
    vec4(0.158203, 0.392578, 0.193359, 0.427734),  // 64: rubber_side_w
    vec4(0.197266, 0.392578, 0.232422, 0.427734),  // 65: rubber_top
    vec4(0.001953, 0.431641, 0.037109, 0.466797),  // 66: stone_bottom
    vec4(0.041016, 0.431641, 0.076172, 0.466797),  // 67: stone_side_e
    vec4(0.080078, 0.431641, 0.115234, 0.466797),  // 68: stone_side_n
    vec4(0.119141, 0.431641, 0.154297, 0.466797),  // 69: stone_side_s
    vec4(0.158203, 0.431641, 0.193359, 0.466797),  // 70: stone_side_w
    vec4(0.197266, 0.431641, 0.232422, 0.466797),  // 71: stone_top
    vec4(0.001953, 0.470703, 0.037109, 0.505859),  // 72: wood_bottom
    vec4(0.041016, 0.470703, 0.076172, 0.505859),  // 73: wood_side_e
    vec4(0.080078, 0.470703, 0.115234, 0.505859),  // 74: wood_side_n
    vec4(0.119141, 0.470703, 0.154297, 0.505859),  // 75: wood_side_s
    vec4(0.158203, 0.470703, 0.193359, 0.505859),  // 76: wood_side_w
    vec4(0.197266, 0.470703, 0.232422, 0.505859)  // 77: wood_top
);

// Calculate atlas UV coordinates from texture index and local UV
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    uint safeTexIndex = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= 78u) {
        safeTexIndex = 54u;  // placeholder_bottom fallback
    }
    vec4 uvBounds = TEXTURE_UVS[safeTexIndex];
    vec2 uvMin = uvBounds.xy;
    vec2 uvMax = uvBounds.zw;
    return mix(uvMin, uvMax, localUV);
}
