#version 450

// Leaf card fragment: cutout ROUNDED leaf silhouette (alpha-tested via discard), colour from the
// leaf texture × baked skylight/block light. No blending → opaque pass, no OIT cost. Sibling of
// grass.frag but with an elliptical (non-pointy) mask.

layout(location = 0) in flat uint vTex;    // leaf texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vCard;       // card-plane coords in [-1,1]
layout(location = 2) in float vSky;        // baked skylight 0..1
layout(location = 3) in vec3  vBlock;      // baked block light 0..1/channel
layout(location = 4) in float vShade;      // per-card brightness variation

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(location = 0) out vec4 outColor;

void main() {
    // Rounded leaf silhouette: an ellipse (slightly narrower in x) — not pointy. Discard outside.
    if (vCard.x * vCard.x * 1.35 + vCard.y * vCard.y > 1.0) discard;

    // Colour from the leaf texture (class bit 15 selects hi/lo array; layer in low 15 bits). Sample
    // by card UV so each card shows leaf detail.
    vec2 uv = vCard * 0.5 + 0.5;
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    vec3 col = (cls == 1u) ? texture(textureArrayHi, vec3(uv, float(layer))).rgb
                           : texture(textureArray,   vec3(uv, float(layer))).rgb;

    // Ambient from skylight (floored), plus additive block light; × per-card variation for depth.
    float ambient = clamp(vSky, 0.14, 1.0);
    vec3 lit = (col * ambient + col * vBlock * 0.5) * vShade;

    outColor = vec4(lit, 1.0);
}
