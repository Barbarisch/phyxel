// Auto-generated GLSL texture atlas header for Phyxel Engine
// Atlas size: 2048x2048
// Texture count: 108

// Atlas constants - these match our generated cube_atlas.h
const uint ATLAS_SIZE = 2048u;           // Atlas dimensions
const uint TEXTURE_SIZE = 64u;          // Individual texture size
const uint PADDING = 1u;                // Padding between textures
const uint TEXTURES_PER_ROW = 6u;       // Calculated from atlas layout

// Pre-calculated UV coordinates for each texture (matches cube_atlas.h exactly)
const vec4 TEXTURE_UVS[108] = vec4[108](
    vec4(0.000488, 0.000488, 0.031738, 0.031738),  // 0: bricks_side_n
    vec4(0.032715, 0.000488, 0.063965, 0.031738),  // 1: cobblestone_side_n
    vec4(0.064941, 0.000488, 0.096191, 0.031738),  // 2: default_side_n
    vec4(0.097168, 0.000488, 0.128418, 0.031738),  // 3: dirt_side_n
    vec4(0.129395, 0.000488, 0.160645, 0.031738),  // 4: glass_side_n
    vec4(0.161621, 0.000488, 0.192871, 0.031738),  // 5: glow_side_n
    vec4(0.000488, 0.032715, 0.031738, 0.063965),  // 6: gold_side_n
    vec4(0.032715, 0.032715, 0.063965, 0.063965),  // 7: grass_side_n
    vec4(0.064941, 0.032715, 0.096191, 0.063965),  // 8: gravel_side_n
    vec4(0.097168, 0.032715, 0.128418, 0.063965),  // 9: ice_side_n
    vec4(0.129395, 0.032715, 0.160645, 0.063965),  // 10: leaf_side_n
    vec4(0.161621, 0.032715, 0.192871, 0.063965),  // 11: log_side_n
    vec4(0.000488, 0.064941, 0.031738, 0.096191),  // 12: metal_side_n
    vec4(0.032715, 0.064941, 0.063965, 0.096191),  // 13: sand_side_n
    vec4(0.064941, 0.064941, 0.096191, 0.096191),  // 14: sandstone_side_n
    vec4(0.097168, 0.064941, 0.128418, 0.096191),  // 15: stone_side_n
    vec4(0.129395, 0.064941, 0.160645, 0.096191),  // 16: stonebricks_side_n
    vec4(0.161621, 0.064941, 0.192871, 0.096191),  // 17: wood_side_n
    vec4(0.000488, 0.097168, 0.031738, 0.128418),  // 18: bricks_side_s
    vec4(0.032715, 0.097168, 0.063965, 0.128418),  // 19: cobblestone_side_s
    vec4(0.064941, 0.097168, 0.096191, 0.128418),  // 20: default_side_s
    vec4(0.097168, 0.097168, 0.128418, 0.128418),  // 21: dirt_side_s
    vec4(0.129395, 0.097168, 0.160645, 0.128418),  // 22: glass_side_s
    vec4(0.161621, 0.097168, 0.192871, 0.128418),  // 23: glow_side_s
    vec4(0.000488, 0.129395, 0.031738, 0.160645),  // 24: gold_side_s
    vec4(0.032715, 0.129395, 0.063965, 0.160645),  // 25: grass_side_s
    vec4(0.064941, 0.129395, 0.096191, 0.160645),  // 26: gravel_side_s
    vec4(0.097168, 0.129395, 0.128418, 0.160645),  // 27: ice_side_s
    vec4(0.129395, 0.129395, 0.160645, 0.160645),  // 28: leaf_side_s
    vec4(0.161621, 0.129395, 0.192871, 0.160645),  // 29: log_side_s
    vec4(0.000488, 0.161621, 0.031738, 0.192871),  // 30: metal_side_s
    vec4(0.032715, 0.161621, 0.063965, 0.192871),  // 31: sand_side_s
    vec4(0.064941, 0.161621, 0.096191, 0.192871),  // 32: sandstone_side_s
    vec4(0.097168, 0.161621, 0.128418, 0.192871),  // 33: stone_side_s
    vec4(0.129395, 0.161621, 0.160645, 0.192871),  // 34: stonebricks_side_s
    vec4(0.161621, 0.161621, 0.192871, 0.192871),  // 35: wood_side_s
    vec4(0.000488, 0.193848, 0.031738, 0.225098),  // 36: bricks_side_e
    vec4(0.032715, 0.193848, 0.063965, 0.225098),  // 37: cobblestone_side_e
    vec4(0.064941, 0.193848, 0.096191, 0.225098),  // 38: default_side_e
    vec4(0.097168, 0.193848, 0.128418, 0.225098),  // 39: dirt_side_e
    vec4(0.129395, 0.193848, 0.160645, 0.225098),  // 40: glass_side_e
    vec4(0.161621, 0.193848, 0.192871, 0.225098),  // 41: glow_side_e
    vec4(0.000488, 0.226074, 0.031738, 0.257324),  // 42: gold_side_e
    vec4(0.032715, 0.226074, 0.063965, 0.257324),  // 43: grass_side_e
    vec4(0.064941, 0.226074, 0.096191, 0.257324),  // 44: gravel_side_e
    vec4(0.097168, 0.226074, 0.128418, 0.257324),  // 45: ice_side_e
    vec4(0.129395, 0.226074, 0.160645, 0.257324),  // 46: leaf_side_e
    vec4(0.161621, 0.226074, 0.192871, 0.257324),  // 47: log_side_e
    vec4(0.000488, 0.258301, 0.031738, 0.289551),  // 48: metal_side_e
    vec4(0.032715, 0.258301, 0.063965, 0.289551),  // 49: sand_side_e
    vec4(0.064941, 0.258301, 0.096191, 0.289551),  // 50: sandstone_side_e
    vec4(0.097168, 0.258301, 0.128418, 0.289551),  // 51: stone_side_e
    vec4(0.129395, 0.258301, 0.160645, 0.289551),  // 52: stonebricks_side_e
    vec4(0.161621, 0.258301, 0.192871, 0.289551),  // 53: wood_side_e
    vec4(0.000488, 0.290527, 0.031738, 0.321777),  // 54: bricks_side_w
    vec4(0.032715, 0.290527, 0.063965, 0.321777),  // 55: cobblestone_side_w
    vec4(0.064941, 0.290527, 0.096191, 0.321777),  // 56: default_side_w
    vec4(0.097168, 0.290527, 0.128418, 0.321777),  // 57: dirt_side_w
    vec4(0.129395, 0.290527, 0.160645, 0.321777),  // 58: glass_side_w
    vec4(0.161621, 0.290527, 0.192871, 0.321777),  // 59: glow_side_w
    vec4(0.000488, 0.322754, 0.031738, 0.354004),  // 60: gold_side_w
    vec4(0.032715, 0.322754, 0.063965, 0.354004),  // 61: grass_side_w
    vec4(0.064941, 0.322754, 0.096191, 0.354004),  // 62: gravel_side_w
    vec4(0.097168, 0.322754, 0.128418, 0.354004),  // 63: ice_side_w
    vec4(0.129395, 0.322754, 0.160645, 0.354004),  // 64: leaf_side_w
    vec4(0.161621, 0.322754, 0.192871, 0.354004),  // 65: log_side_w
    vec4(0.000488, 0.354980, 0.031738, 0.386230),  // 66: metal_side_w
    vec4(0.032715, 0.354980, 0.063965, 0.386230),  // 67: sand_side_w
    vec4(0.064941, 0.354980, 0.096191, 0.386230),  // 68: sandstone_side_w
    vec4(0.097168, 0.354980, 0.128418, 0.386230),  // 69: stone_side_w
    vec4(0.129395, 0.354980, 0.160645, 0.386230),  // 70: stonebricks_side_w
    vec4(0.161621, 0.354980, 0.192871, 0.386230),  // 71: wood_side_w
    vec4(0.000488, 0.387207, 0.031738, 0.418457),  // 72: bricks_top
    vec4(0.032715, 0.387207, 0.063965, 0.418457),  // 73: cobblestone_top
    vec4(0.064941, 0.387207, 0.096191, 0.418457),  // 74: default_top
    vec4(0.097168, 0.387207, 0.128418, 0.418457),  // 75: dirt_top
    vec4(0.129395, 0.387207, 0.160645, 0.418457),  // 76: glass_top
    vec4(0.161621, 0.387207, 0.192871, 0.418457),  // 77: glow_top
    vec4(0.000488, 0.419434, 0.031738, 0.450684),  // 78: gold_top
    vec4(0.032715, 0.419434, 0.063965, 0.450684),  // 79: grass_top
    vec4(0.064941, 0.419434, 0.096191, 0.450684),  // 80: gravel_top
    vec4(0.097168, 0.419434, 0.128418, 0.450684),  // 81: ice_top
    vec4(0.129395, 0.419434, 0.160645, 0.450684),  // 82: leaf_top
    vec4(0.161621, 0.419434, 0.192871, 0.450684),  // 83: log_top
    vec4(0.000488, 0.451660, 0.031738, 0.482910),  // 84: metal_top
    vec4(0.032715, 0.451660, 0.063965, 0.482910),  // 85: sand_top
    vec4(0.064941, 0.451660, 0.096191, 0.482910),  // 86: sandstone_top
    vec4(0.097168, 0.451660, 0.128418, 0.482910),  // 87: stone_top
    vec4(0.129395, 0.451660, 0.160645, 0.482910),  // 88: stonebricks_top
    vec4(0.161621, 0.451660, 0.192871, 0.482910),  // 89: wood_top
    vec4(0.000488, 0.483887, 0.031738, 0.515137),  // 90: bricks_bottom
    vec4(0.032715, 0.483887, 0.063965, 0.515137),  // 91: cobblestone_bottom
    vec4(0.064941, 0.483887, 0.096191, 0.515137),  // 92: default_bottom
    vec4(0.097168, 0.483887, 0.128418, 0.515137),  // 93: dirt_bottom
    vec4(0.129395, 0.483887, 0.160645, 0.515137),  // 94: glass_bottom
    vec4(0.161621, 0.483887, 0.192871, 0.515137),  // 95: glow_bottom
    vec4(0.000488, 0.516113, 0.031738, 0.547363),  // 96: gold_bottom
    vec4(0.032715, 0.516113, 0.063965, 0.547363),  // 97: grass_bottom
    vec4(0.064941, 0.516113, 0.096191, 0.547363),  // 98: gravel_bottom
    vec4(0.097168, 0.516113, 0.128418, 0.547363),  // 99: ice_bottom
    vec4(0.129395, 0.516113, 0.160645, 0.547363),  // 100: leaf_bottom
    vec4(0.161621, 0.516113, 0.192871, 0.547363),  // 101: log_bottom
    vec4(0.000488, 0.548340, 0.031738, 0.579590),  // 102: metal_bottom
    vec4(0.032715, 0.548340, 0.063965, 0.579590),  // 103: sand_bottom
    vec4(0.064941, 0.548340, 0.096191, 0.579590),  // 104: sandstone_bottom
    vec4(0.097168, 0.548340, 0.128418, 0.579590),  // 105: stone_bottom
    vec4(0.129395, 0.548340, 0.160645, 0.579590),  // 106: stonebricks_bottom
    vec4(0.161621, 0.548340, 0.192871, 0.579590)  // 107: wood_bottom
);

// Calculate atlas UV coordinates from texture index and local UV
vec2 getAtlasUV(uint texIndex, vec2 localUV) {
    // Handle invalid/placeholder texture
    uint safeTexIndex = texIndex;
    if (texIndex == 0xFFFFu || texIndex >= 108u) {  // Invalid texture index
        safeTexIndex = 6u;      // Use placeholder_bottom texture
    }
    
    // Get pre-calculated UV bounds for this texture
    vec4 uvBounds = TEXTURE_UVS[safeTexIndex];
    vec2 uvMin = uvBounds.xy;
    vec2 uvMax = uvBounds.zw;
    
    // Interpolate between min and max based on local UV
    return mix(uvMin, uvMax, localUV);
}
