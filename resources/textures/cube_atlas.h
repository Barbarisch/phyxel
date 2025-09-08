#pragma once
// Auto-generated texture atlas header for Phyxel Engine
// Atlas size: 128x128
// Texture count: 12

#include <unordered_map>
#include <string>

namespace Phyxel {
namespace TextureAtlas {

// Atlas constants
constexpr int ATLAS_SIZE = 128;
constexpr int TEXTURE_SIZE = 18;
constexpr int PADDING = 2;
constexpr int TEXTURES_PER_ROW = 5;

// Texture UV coordinates
struct TextureUV {
    float u_min, v_min, u_max, v_max;
};

// Texture indices (for shader uniforms)
enum TextureIndex {
    TEX_GRASSDIRT_BOTTOM = 0,
    TEX_GRASSDIRT_SIDE_E = 1,
    TEX_GRASSDIRT_SIDE_N = 2,
    TEX_GRASSDIRT_SIDE_S = 3,
    TEX_GRASSDIRT_SIDE_W = 4,
    TEX_GRASSDIRT_TOP = 5,
    TEX_PLACEHOLDER_BOTTOM = 6,
    TEX_PLACEHOLDER_SIDE_E = 7,
    TEX_PLACEHOLDER_SIDE_N = 8,
    TEX_PLACEHOLDER_SIDE_S = 9,
    TEX_PLACEHOLDER_SIDE_W = 10,
    TEX_PLACEHOLDER_TOP = 11,
    TEXTURE_COUNT = 12
};

// UV lookup table
const std::unordered_map<std::string, TextureUV> TEXTURE_UVS = {
    {"grassdirt_bottom", {0.015625f, 0.015625f, 0.156250f, 0.156250f}},
    {"grassdirt_side_e", {0.187500f, 0.015625f, 0.328125f, 0.156250f}},
    {"grassdirt_side_n", {0.359375f, 0.015625f, 0.500000f, 0.156250f}},
    {"grassdirt_side_s", {0.531250f, 0.015625f, 0.671875f, 0.156250f}},
    {"grassdirt_side_w", {0.703125f, 0.015625f, 0.843750f, 0.156250f}},
    {"grassdirt_top", {0.015625f, 0.187500f, 0.156250f, 0.328125f}},
    {"placeholder_bottom", {0.187500f, 0.187500f, 0.328125f, 0.328125f}},
    {"placeholder_side_e", {0.359375f, 0.187500f, 0.500000f, 0.328125f}},
    {"placeholder_side_n", {0.531250f, 0.187500f, 0.671875f, 0.328125f}},
    {"placeholder_side_s", {0.703125f, 0.187500f, 0.843750f, 0.328125f}},
    {"placeholder_side_w", {0.015625f, 0.359375f, 0.156250f, 0.500000f}},
    {"placeholder_top", {0.187500f, 0.359375f, 0.328125f, 0.500000f}},
};

// Get texture index by name
inline int getTextureIndex(const std::string& name) {
    static const std::unordered_map<std::string, int> indices = {
        {"grassdirt_bottom", 0},
        {"grassdirt_side_e", 1},
        {"grassdirt_side_n", 2},
        {"grassdirt_side_s", 3},
        {"grassdirt_side_w", 4},
        {"grassdirt_top", 5},
        {"placeholder_bottom", 6},
        {"placeholder_side_e", 7},
        {"placeholder_side_n", 8},
        {"placeholder_side_s", 9},
        {"placeholder_side_w", 10},
        {"placeholder_top", 11},
    };
    auto it = indices.find(name);
    return (it != indices.end()) ? it->second : -1;
}

}} // namespace TextureAtlas
}} // namespace Phyxel
