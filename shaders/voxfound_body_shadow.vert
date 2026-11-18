#version 450
// docs/VoxelFoundationPlan.md §6.7 "Drawing", F5.3 — shadow CASTING for loose bodies (BodyFace, 12 B).
//
// The twin of voxfound_face_shadow.vert: same push constants and set 0 as the chunk shadow pipeline
// (ShadowMap::createPipeline), so it shares that pipeline layout; same 36-index both-windings draw under BACK cull (M5).
// Set 0 binding 0 = the body pose buffer (BodyDraw.h BodyPose, 64 B), indexed poses[drawIndexBase + body] (the frame's
// slot). Corner table, extents and the transform MUST match voxfound_body.vert, or the shadow detaches from the piece.

layout(location = 0) in uint vertexID;
layout(location = 1) in uint inFaceA;
layout(location = 2) in uint inFaceB;
layout(location = 3) in uint inBody;

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
    vec3 chunkBaseOffset;   // unused (poses are camera-relative already)
    uint useChunkDataSsbo;  // unused
    uint drawIndexBase;     // this frame's first pose
} pushConstants;

struct BodyPose {
    vec4 q;
    vec4 t;
    ivec4 uvOrigin;
    vec4 areaAbs;
};
layout(std430, set = 0, binding = 0) readonly buffer VoxFoundBodyPoses {
    BodyPose poses[];
};

vec3 rotateByQuat(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

void main() {
    const BodyPose P = poses[pushConstants.drawIndexBase + inBody];
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
    const vec3 worldPos = P.t.xyz + rotateByQuat(P.q, (o + faceOffset * sizeVec) * P.t.w);
    gl_Position = pushConstants.lightSpaceMatrix * vec4(worldPos, 1.0);
}
