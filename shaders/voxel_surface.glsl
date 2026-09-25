// voxel_surface.glsl — what BOTH the static opaque pass (voxel.frag) and its depth prepass
// (voxel_depth.frag) must agree on: which fragments of a static voxel face exist at all.
// docs/PerfProgram2026-09.md, P-DP.
//
// WHY THIS FILE EXISTS. The depth prepass writes the depth the shaded pass then tests against with
// an or-equal compare. If the prepass kept a fragment the shaded pass discards (a glass face, a
// cutout texel, a mirror face), that fragment's depth would hide whatever lies behind it and the
// frame would show a hole. The texture coordinates, gradients and the cutout threshold therefore
// live here exactly once, and both shaders call them.
//
// Requires, declared by the including shader BEFORE this include:
//   voxel_world.glsl (phxAtlasSelect, worldFaceUV), which itself needs atlasUVs (binding 4),
//   textureArray (binding 1) and textureArrayHi (binding 5).

#ifndef PHX_VOXEL_SURFACE_GLSL
#define PHX_VOXEL_SURFACE_GLSL

// Texels with alpha below this are cut out (never drawn, never in depth).
const float PHX_CUTOUT_ALPHA = 0.1;

// Faces the static opaque pass never draws, decided by flags alone:
//   bit 1  (2)       transparent material: drawn only by the OIT pass (docs/GlassTransparency.md §1)
//   bit 10 (1 << 10) mirror: drawn only by the mirror pass
bool phxStaticFaceSkippedByFlags(uint flags) {
    return (flags & 2u) != 0u || (flags & (1u << 10u)) != 0u;
}

// Cheap 2D hash -> [0,1). Used to pick a per-world-cell tile rotation (Phase A).
float hash21(vec2 p) {
    p = fract(p * vec2(127.1, 311.7));
    p += dot(p, p + 34.23);
    return fract(p.x * p.y);
}

// Sampling coordinates + explicit gradients for a static voxel face. When `varied` (materials.json
// "varied", docs/VoxelOrientation.md Phase A) each world cell's tile is hash-rotated (90deg step +
// optional flip); the gradients are rotated with it so mips stay correct across the tile seam.
void phxVoxelSampleCoords(vec2 uv, bool varied, vec3 worldPos, vec3 faceNormal,
                          out vec2 suv, out vec2 gx, out vec2 gy, out int rotStep, out bool flipped) {
    suv = uv;
    gx  = dFdx(uv);
    gy  = dFdy(uv);
    rotStep = 0;
    flipped = false;
    if (varied) {
        vec2 p = worldFaceUV(worldPos, faceNormal);
        float h = hash21(floor(p) + 0.5);
        rotStep = int(floor(h * 4.0)) & 3;        // 0/90/180/270
        flipped = fract(h * 16.0) > 0.5;
        vec2 lp  = fract(p);
        vec2 dpx = dFdx(p);
        vec2 dpy = dFdy(p);
        if (flipped) { lp.x = 1.0 - lp.x; dpx.x = -dpx.x; dpy.x = -dpy.x; }
        vec2 ctr = lp - 0.5;
        if      (rotStep == 1) { ctr = vec2(-ctr.y, ctr.x); dpx = vec2(-dpx.y, dpx.x); dpy = vec2(-dpy.y, dpy.x); }
        else if (rotStep == 2) { ctr = -ctr;                dpx = -dpx;                dpy = -dpy;                }
        else if (rotStep == 3) { ctr = vec2(ctr.y, -ctr.x); dpx = vec2(dpx.y, -dpx.x); dpy = vec2(dpy.y, -dpy.x); }
        suv = ctr + 0.5;
        gx = dpx; gy = dpy;
    }
}

// The face's albedo at the given coordinates, through the class-aware atlas path.
vec4 phxSampleVoxelAlbedoGrad(uint texIndex, vec2 suv, vec2 gx, vec2 gy) {
    uint c; float L;
    phxAtlasSelect(texIndex, c, L);
    return (c == 1u) ? textureGrad(textureArrayHi, vec3(suv, L), gx, gy)
                     : textureGrad(textureArray,   vec3(suv, L), gx, gy);
}

#endif // PHX_VOXEL_SURFACE_GLSL
