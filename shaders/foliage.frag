#version 450

// Leaf card fragment: cutout LEAF-CLUSTER silhouette alpha-tested from the leaf texture's alpha
// channel (baked by tools/leaf_forge.py — chunky voxel-native masks; was a flat math ellipse).
// Colour from the leaf texture × baked skylight/block light. No blending → opaque pass, no OIT
// cost. Sibling of grass.frag.

layout(location = 0) in flat uint vTex;    // leaf texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vCard;       // card-plane coords in [-1,1]
layout(location = 2) in float vSky;        // baked skylight 0..1
layout(location = 3) in vec3  vBlock;      // baked block light 0..1/channel
layout(location = 4) in float vShade;      // per-card brightness variation
layout(location = 5) in flat uint vMaskV;  // per-card mask variant (bit0 flipX, bit1 flipY, bit2 swap)

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(location = 0) out vec4 outColor;

void main() {
    // Per-card mask orientation: flip/swap the UV so one baked mask yields 8 variants.
    vec2 uv = vCard * 0.5 + 0.5;
    if ((vMaskV & 1u) != 0u) uv.x = 1.0 - uv.x;
    if ((vMaskV & 2u) != 0u) uv.y = 1.0 - uv.y;
    if ((vMaskV & 4u) != 0u) uv = uv.yx;

    // Colour + cutout mask from the leaf texture (class bit 15 selects hi/lo array; layer in low
    // 15 bits). Alpha carries the leaf-forge cluster silhouette — discard the gaps.
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    vec4 texel = (cls == 1u) ? texture(textureArrayHi, vec3(uv, float(layer)))
                             : texture(textureArray,   vec3(uv, float(layer)));
    if (texel.a < 0.5) discard;
    vec3 col = texel.rgb;

    // Ambient from skylight (floored), plus additive block light; × per-card variation for depth.
    float ambient = clamp(vSky, 0.14, 1.0);
    vec3 lit = (col * ambient + col * vBlock * 0.5) * vShade;

    outColor = vec4(lit, 1.0);
}
