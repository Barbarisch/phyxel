#version 450
#extension GL_ARB_shader_draw_parameters : require
// docs/VoxelFoundationPlan.md F2.4, §4.6 item 6 — shadow CASTING for the 1/8 m faces (PackedFace).
//
// The twin of shadow.vert for the voxel foundation's 8 B face record: same push constants and set 0 as the chunk
// shadow pipeline (ShadowMap::createPipeline), so it shares that pipeline layout; same 36-index both-windings draw
// under BACK cull (the M5 rule: face quads are wound for the camera, so only the both-windings cube survives
// back-culling relative to the light). Corner table and extents MUST match voxfound_face.vert, or the shadow
// detaches from the faces that throw it.

layout(location = 0) in uint vertexID;   // face corner 0-3 (shared vertex buffer)
layout(location = 1) in uint inFaceA;    // x 0-7 | y 8-15 | z 16-23 | axis 24-25 | dir>0 bit 26
layout(location = 2) in uint inFaceB;    // du-1 0-7 | dv-1 8-15 | material 16-23 (8 B record, F2.5)

//
// F2.12 (§4.6 item 6): the same shader serves the ONE indirect multidraw over the face clusters (ClusterCullGpu,
// culled to the cascade's light frustum). Then useChunkDataSsbo = 1, chunkBaseOffset = the region anchor
// (camera-relative) and origins[drawIndexBase + gl_DrawIDARB] = the draw's chunk origin relative to that anchor (whole
// metres, exact) — the shadow pipeline's own set 0 (ShadowMap chunk data layout), bound to the preview's table.

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;   // per-chunk draws: CAMERA-RELATIVE origin of voxel (0,0,0); multidraw: the anchor
    uint useChunkDataSsbo;  // 0 = per-chunk draws; 1 = multidraw, add origins[drawIndexBase + gl_DrawIDARB]
    uint drawIndexBase;
} pushConstants;

layout(std430, set = 0, binding = 0) readonly buffer ChunkData {
    vec4 origins[];
} chunkData;

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
    vec3 base = pushConstants.chunkBaseOffset;
    if (pushConstants.useChunkDataSsbo != 0u) base += chunkData.origins[pushConstants.drawIndexBase + uint(gl_DrawIDARB)].xyz;
    const vec3 worldPos = base + (o + faceOffset * sizeVec) * kVoxel;
    gl_Position = pushConstants.lightSpaceMatrix * vec4(worldPos, 1.0);
}
