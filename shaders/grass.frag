#version 450

// Grass blade fragment: cutout (alpha-tested via discard) tapered blade silhouette, colour derived
// from the voxel's grass-top texture, shaded by baked skylight + block light. No blending → renders
// in the opaque pass with no OIT sort cost. See GrassRenderPipeline / the grass plan.

layout(location = 0) in flat uint vTex;    // grass texture index (class bit 15 + layer bits 0-14)
layout(location = 1) in vec2  vUV;         // colour-sample UV
layout(location = 2) in float vGrad;       // 0 base .. 1 tip
layout(location = 3) in float vSide;       // -1..1 across blade width
layout(location = 4) in float vSky;        // baked skylight 0..1
layout(location = 5) in vec3  vBlock;      // baked block light 0..1/channel

layout(set = 0, binding = 1) uniform sampler2DArray textureArray;    // 512px albedo class
layout(set = 0, binding = 5) uniform sampler2DArray textureArrayHi;  // 1024px albedo class

layout(location = 0) out vec4 outColor;

void main() {
    // Blade silhouette: triangular taper — full width at base, pinches to a point at the tip.
    float taper = 1.0 - vGrad * 0.92;
    if (abs(vSide) > taper) discard;

    // Colour from the grass-top texture (class bit 15 selects hi/lo array; layer in low 15 bits).
    uint cls   = (vTex >> 15) & 1u;
    uint layer = vTex & 0x7FFFu;
    vec3 col = (cls == 1u) ? texture(textureArrayHi, vec3(vUV, float(layer))).rgb
                           : texture(textureArray,   vec3(vUV, float(layer))).rgb;

    // Fake AO: darker at the base, slightly brighter toward the tip.
    float ao = mix(0.5, 1.08, vGrad);

    // Ambient from skylight (floor so night/shadow isn't pure black), plus additive block light.
    float ambient = clamp(vSky, 0.12, 1.0);
    vec3 lit = col * ao * ambient + col * vBlock * 0.5;

    outColor = vec4(lit, 1.0);
}
