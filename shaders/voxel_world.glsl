// voxel_world.glsl — world-space and atlas helpers shared by voxel.frag (opaque pass) and
// transparent_voxel.frag (OIT pass). docs/GlassTransparency.md §13.12, §13.17.
//
// WHY THIS FILE EXISTS. The transparent pass kept private copies of opaque-pass code, and every one
// of them drifted silently:
//   * its world position was `inWorldPos + cameraWorld`, a float sum that loses precision far from
//     the origin, where voxel.frag uses the exact chunk-origin form below — a crack seeded from the
//     former would not match stone's pattern and would shimmer at large coordinates (§13.12);
//   * it sampled ONE texture array with a bounds check against the 512-class count, so every
//     1024-class material (Glass included) fell back to the placeholder layer (§13.17);
//   * it had no worldFaceUV at all, which crack.glsl needs.
// The rule is now: anything both passes compute lives HERE, exactly once.
//
// Requires, declared by the including shader BEFORE this include:
//   atlasUVs (binding 4: count512, fallbackIndex, count1024, _pad1, textureUVs[])
//   textureArray (binding 1, 512 px class) and textureArrayHi (binding 5, 1024 px class)

#ifndef PHX_VOXEL_WORLD_GLSL
#define PHX_VOXEL_WORLD_GLSL

// Exact absolute world position of a fragment. `chunkBaseAbs` is the chunk origin in exact absolute
// coordinates; `worldPosRel` and `chunkBaseRel` are both camera-relative, so their difference is the
// fragment's small in-chunk offset and never loses precision. Seed anything that must be a pure
// function of world position (cracks, varied tiling) from this — never from inWorldPos + cameraWorld.
vec3 phxWorldPosAbs(vec3 chunkBaseAbs, vec3 worldPosRel, vec3 chunkBaseRel) {
    return chunkBaseAbs + (worldPosRel - chunkBaseRel);
}

// World-aligned 2-D coordinates for a face: the lattice cracks and varied tiling are laid out on.
vec2 worldFaceUV(vec3 wp, vec3 n) {
    vec3 a = abs(n);
    if (a.y >= a.x && a.y >= a.z) return wp.xz;   // top/bottom
    if (a.x >= a.z)               return wp.zy;   // +/-X
    return wp.xy;                                 // +/-Z
}

// Resolve a packed texture index to (class, layer). Bit 15 selects the resolution class
// (0 = 512 px, 1 = 1024 px); bits 0..14 are the layer within that class. Out-of-range and the
// 0xFFFF sentinel fall back to the placeholder layer in the 512 class.
void phxAtlasSelect(uint texIndex, out uint cls, out float layer) {
    uint c = (texIndex >> 15) & 1u;
    uint l = texIndex & 0x7FFFu;
    uint count = (c == 1u) ? atlasUVs.count1024 : atlasUVs.count512;
    bool fb = (texIndex == 0xFFFFu || l >= count);
    cls   = fb ? 0u : c;
    layer = fb ? float(atlasUVs.fallbackIndex) : float(l);
}

// Index into the per-material props array (textureUVs), which is laid out class 0 then class 1.
uint phxAtlasGlobalIndex(uint texIndex) {
    uint c = (texIndex >> 15) & 1u;
    uint l = texIndex & 0x7FFFu;
    return (c == 1u) ? atlasUVs.count512 + l : l;
}

// Fracture character for this material (P5): props stride 2, [gi*2+1].x = crackStyle.
float phxCrackStyleOf(uint texIndex) {
    uint gi = phxAtlasGlobalIndex(texIndex);
    return max(atlasUVs.textureUVs[gi * 2u + 1u].x, 0.15);
}

// Albedo through the class-aware path — the only correct way to sample a voxel's own texture.
vec4 phxSampleAlbedo(uint texIndex, vec2 uv) {
    uint cls; float layer;
    phxAtlasSelect(texIndex, cls, layer);
    return (cls == 1u) ? texture(textureArrayHi, vec3(uv, layer))
                       : texture(textureArray,   vec3(uv, layer));
}

#endif // PHX_VOXEL_WORLD_GLSL
