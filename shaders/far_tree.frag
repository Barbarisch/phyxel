#version 450

// Far-tree impostor cards: procedural tree silhouettes, cutout-discarded. No textures — at
// 300u+ a species-tinted shape with a vertical shade gradient is all that resolves, and it
// matches the engine's voxel aesthetic better than photographic billboards would.

layout(location = 0) in vec2 vUV;           // x: -1..1 across, y: 0 base .. 1 tip
layout(location = 1) in flat uint vPacked;  // bits 0-1 class | 8-15 R | 16-23 G | 24-31 B
layout(location = 2) in flat float vShade;
layout(location = 3) in flat float vFade;   // dither factor: 0 dissolved .. 1 solid

layout(location = 0) out vec4 outColor;

// 4x4 ordered Bayer dither: stable per-pixel threshold, no temporal shimmer.
float bayer4(vec2 p) {
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0,
                                  3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    ivec2 ip = ivec2(mod(p, 4.0));
    return (m[ip.x + ip.y * 4] + 0.5) / 16.0;
}

void main() {
    if (vFade < bayer4(gl_FragCoord.xy)) discard;   // screen-door fade — size never changes
    uint cls = vPacked & 0x3u;
    vec3 tint = vec3(float((vPacked >> 8) & 0xFFu),
                     float((vPacked >> 16) & 0xFFu),
                     float((vPacked >> 24) & 0xFFu)) / 255.0;
    const vec3 kTrunk = vec3(0.36, 0.28, 0.20);

    float u = abs(vUV.x);   // 0 at the axis, 1 at the card edge
    float v = vUV.y;        // 0 base, 1 tip

    bool canopy = false;
    bool trunk  = false;

    if (cls == 1u) {
        // CONIFER: triangular crown from ~18% height to the tip, short trunk below.
        if (v > 0.16) {
            float half_ = (1.0 - (v - 0.16) / 0.84);
            canopy = u < half_ * 0.92;
        }
        trunk = (v <= 0.20) && (u < 0.10);
    } else if (cls == 0u) {
        // BROADLEAF: elliptical canopy over a trunk.
        vec2 c = vec2(vUV.x / 0.95, (v - 0.62) / 0.40);
        canopy = dot(c, c) < 1.0;
        trunk  = (v <= 0.45) && (u < 0.09);
    } else if (cls == 2u) {
        // PALM: small high crown, tall thin trunk.
        vec2 c = vec2(vUV.x / 0.9, (v - 0.82) / 0.20);
        canopy = dot(c, c) < 1.0;
        trunk  = (v <= 0.85) && (u < 0.07);
    } else {
        // DEAD/BARE: tapering snag, no canopy.
        trunk = u < mix(0.12, 0.02, v);
    }

    if (!canopy && !trunk) discard;

    // Canopy wins where the shapes overlap (trunk disappears into foliage).
    vec3 col = canopy ? tint : kTrunk;
    // Vertical gradient: darker base reads as self-shadowed foliage mass.
    col *= vShade * mix(0.72, 1.05, v);
    outColor = vec4(col, 1.0);
}
