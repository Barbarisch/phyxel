#version 450
// docs/VoxelFoundationPlan.md F2.14 (§4.6 item 7): the 1/8 m face CASTER with the LOD levels -- the twin of
// voxfound_face_shadow.vert (same corner table, extents and 36-index both-windings draw, M5) that hands the
// CAMERA-RELATIVE caster position to voxfound_face_shadow_lod.frag, which applies the main view's per-fragment level
// test (voxel.frag F2.14) at the caster fragment. Per-chunk draws only (one level per draw): a face record of level k
// is in level-0 units already (lodScaleFace), so the geometry code is unchanged.

layout(location = 0) in uint vertexID;   // face corner 0-3 (shared vertex buffer)
layout(location = 1) in uint inFaceA;    // x 0-7 | y 8-15 | z 16-23 | axis 24-25 | dir>0 bit 26
layout(location = 2) in uint inFaceB;    // du-1 0-7 | dv-1 8-15 | material 16-23 (8 B record, F2.5)

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;   // CAMERA-RELATIVE origin of voxel (0,0,0) of this draw
    uint lodBits;           // voxel.frag flag bits: maxLevel << 20 | on << 23 | code << 24
    uint level;             // this draw's level
    float lodFloor;         // log2(8 * shadow texel): levels the map cannot tell apart from 1/8 m
} pushConstants;

layout(location = 0) out vec3 outCamRel;
const float kVoxel = 1.0 / 8.0;

void main() {
    const uint axis = (inFaceA >> 24) & 3u;
    const bool pos = ((inFaceA >> 26) & 1u) != 0u;
    const vec3 o = vec3(float(inFaceA & 0xFFu), float((inFaceA >> 8) & 0xFFu), float((inFaceA >> 16) & 0xFFu));
    const float du = float((inFaceB & 0xFFu) + 1u);
    const float dv = float(((inFaceB >> 8) & 0xFFu) + 1u);

    uint faceID;
    if (axis == 2u)      faceID = pos ? 0u : 1u;
    else if (axis == 0u) faceID = pos ? 2u : 3u;
    else                 faceID = pos ? 4u : 5u;

    const float b0 = float((vertexID >> 0) & 1u), b1 = float((vertexID >> 1) & 1u);
    vec3 faceOffset;
    if (faceID == 0u)      faceOffset = vec3(b0, b1, 1.0);
    else if (faceID == 1u) faceOffset = vec3(1.0 - b0, b1, 0.0);
    else if (faceID == 2u) faceOffset = vec3(1.0, b1, 1.0 - b0);
    else if (faceID == 3u) faceOffset = vec3(0.0, b1, b0);
    else if (faceID == 4u) faceOffset = vec3(b0, 1.0, 1.0 - b1);
    else                   faceOffset = vec3(b0, 0.0, b1);

    vec3 sizeVec = vec3(1.0);
    sizeVec[(axis + 1u) % 3u] = du;
    sizeVec[(axis + 2u) % 3u] = dv;
    const vec3 base = pushConstants.chunkBaseOffset;
    const vec3 worldPos = base + (o + faceOffset * sizeVec) * kVoxel;
    outCamRel = worldPos;
    gl_Position = pushConstants.lightSpaceMatrix * vec4(worldPos, 1.0);
}
