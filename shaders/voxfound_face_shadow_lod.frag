#version 450
// docs/VoxelFoundationPlan.md F2.14 (§4.6 item 7): the per-fragment level test for the 1/8 m face CASTERS. The same
// keep test as voxel.frag's (and VoxFound::lodKeeps, the CPU reference): L = log2(camera distance of THIS caster
// fragment) + c, a 4x4 Bayer value of the shadow texel, band 1/2 level, slack 1/4 level -- so a caster is drawn at the
// level the camera sees it at. "From the light's view": L is floored at lodFloor = log2(8 * texel), the level whose
// voxel the shadow map cannot tell from 1/8 m anyway.

layout(location = 0) in vec3 inCamRel;

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;
    uint lodBits;
    uint level;
    float lodFloor;
} pushConstants;

void main() {
    const uint flags = pushConstants.lodBits;
    const float lodC = float(flags >> 24u) / 8.0 - 16.0;
    const int lodLevel = int(pushConstants.level), lodMax = int((flags >> 20u) & 7u);
    // kLodCasterLag (Mesher.h) = 1: a caster goes coarse only where every screen pixel already has (the dither picks
    // per screen pixel there and per shadow texel here; a coarser caster than the surface shadows it in a stipple)
    const float lodL = max(log2(max(length(inCamRel), 1e-3)) + lodC - 1.0, pushConstants.lodFloor);
    const ivec2 bp = ivec2(gl_FragCoord.xy) & 3;
    const int bayer[16] = int[16](0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5);
    const float lodU = (float(bayer[bp.y * 4 + bp.x]) + 0.5) / 16.0;
    const float lodD = 0.5 * (lodU - 0.5);
    const int lodLo = clamp(int(floor(lodL - 0.25 + lodD)), 0, lodMax);
    const int lodHi = clamp(int(floor(lodL + 0.25 + lodD)), 0, lodMax);
    if (lodLevel < lodLo || lodLevel > lodHi) discard;
}
