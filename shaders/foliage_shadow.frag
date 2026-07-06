#version 450

// Foliage shadow fragment: depth-only, alpha-tested with the SAME leaf-forge cutout mask as the
// visible pass (foliage.frag) so canopy shadows are dappled leaf clusters, not solid card quads.
// No color attachment — kept fragments just write shadow-map depth.

layout(location = 0) in flat uint vTex;    // leaf texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vCard;       // card-plane coords in [-1,1]
layout(location = 2) in flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

void main() {
    vec2 uv = vCard * 0.5 + 0.5;
    if ((vMaskV & 1u) != 0u) uv.x = 1.0 - uv.x;
    if ((vMaskV & 2u) != 0u) uv.y = 1.0 - uv.y;
    if ((vMaskV & 4u) != 0u) uv = uv.yx;

    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    float a = (cls == 1u) ? texture(textureArrayHi, vec3(uv, float(layer))).a
                          : texture(textureArray,   vec3(uv, float(layer))).a;
    if (a < 0.5) discard;
}
