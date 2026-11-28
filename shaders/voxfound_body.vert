#version 450
//
// voxfound_body.vert — docs/VoxelFoundationPlan.md §6.7 "Drawing", F5.3: loose bodies drawn with ONE transform each.
// voxfound_face.vert with the body's pose: same corner table, faceID order, extents, UV orientation and voxel.frag.
//
// Instance (binding 1, 12 bytes, core/voxfound/BodyDraw.h BodyFace): the 8 B PackedFace record (body-grid-local)
// + the body index. Faces are built once when the body is made; only the pose buffer changes per frame.
// Set 1: binding 0 = the face texture table (Preview.h faceTex, as the preview), binding 1 = the poses (BodyPose):
//   q (rotation x y z w) | t (xyz: the body grid's corner (0,0,0), CAMERA-RELATIVE metres, from double on the CPU;
//   w: voxel size m) | uvOrigin (the body's grid origin in its source area, voxels) | areaAbs (the area's origin,
//   absolute metres).
// A corner lands at t.xyz + rotate(q, local): CPU mirror BodyDraw.cpp bodyCornerRel (unit-tested, D3/D4).
// Texture (§4.3): a pure function of where the voxel WAS in its area when it broke off (rest frame), so a piece
// carries its texture with it. Varied materials: texCoord carries the REST-frame world-face coordinates and flags
// bit 16 (with the LOD bit 23 clear) tells voxel.frag to pick the tile from them (today's moving voxels turn varied
// off instead, which would pop the texture at the break).

layout(location = 0) in uint vertexID;
layout(location = 1) in uint inFaceA;
layout(location = 2) in uint inFaceB;
layout(location = 3) in uint inBody;

layout(std430, set = 1, binding = 0) readonly buffer VoxFoundFaceTex {
    uint faceTex[];   // 256 * 6
};

struct BodyPose {
    vec4 q;
    vec4 t;
    ivec4 uvOrigin;
    vec4 areaAbs;
};
layout(std430, set = 1, binding = 1) readonly buffer VoxFoundBodyPoses {
    BodyPose poses[];
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 sunDirection;
    vec3 sunColor;
    uint numInstances;
    float ambientLight;
    float emissiveMultiplier;
    vec3 cameraPosition;
    mat4 reflectedViewProj;
    float elapsedTime;
    mat4 viewProj;
    mat4 biasedLightSpace;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 unused0;
    uint mutate;           // F5.3 gate mutation: != 0 adds a 0.5 degree turn about y to every body
    vec3 unused1;
    float unused2;
} pushConstants;

layout(location = 0) out flat uint textureIndex;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec4 shadowCoord;
layout(location = 3) out flat uint flags;
layout(location = 4) out vec3 outNormal;
layout(location = 5) out vec3 outWorldPos;
layout(location = 6) out float vSkyLight;
layout(location = 8) out vec3  vTint;
layout(location = 9) out flat uint vState;
layout(location = 10) out flat vec3 vChunkBaseAbs;
layout(location = 11) out flat vec3 vChunkBaseRel;

vec3 rotateByQuat(vec4 q, vec3 v) { return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v); }

// voxel_world.glsl worldFaceUV, for the axis-aligned REST normal
vec2 restFaceUV(vec3 wp, uint axis) {
    if (axis == 1u) return wp.xz;
    if (axis == 0u) return wp.zy;
    return wp.xy;
}

void main() {
    const BodyPose P = poses[inBody];
    vec4 q = P.q;
    if (pushConstants.mutate != 0u) {   // q * (0.5 degrees about y)
        const vec4 m = vec4(0.0, sin(0.5 * 0.00872665), 0.0, cos(0.5 * 0.00872665));
        q = vec4(q.w * m.xyz + m.w * q.xyz + cross(q.xyz, m.xyz), q.w * m.w - dot(q.xyz, m.xyz));
    }
    const float vs = P.t.w;
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
    const vec3 cornerVox = o + faceOffset * sizeVec;          // body-grid voxels, exact
    const vec3 worldPos = P.t.xyz + rotateByQuat(q, cornerVox * vs);   // camera-relative

    vec3 n;
    if (faceID == 0u)      n = vec3(0.0, 0.0, 1.0);
    else if (faceID == 1u) n = vec3(0.0, 0.0, -1.0);
    else if (faceID == 2u) n = vec3(1.0, 0.0, 0.0);
    else if (faceID == 3u) n = vec3(-1.0, 0.0, 0.0);
    else if (faceID == 4u) n = vec3(0.0, 1.0, 0.0);
    else                   n = vec3(0.0, -1.0, 0.0);
    outNormal = rotateByQuat(q, n);

    // the rest frame: metres from the area origin where this corner was when the body broke off (exact, k * vs)
    const vec3 restM = (vec3(P.uvOrigin.xyz) + cornerVox) * vs;
    const uint texWord = faceTex[((inFaceB >> 16) & 0xFFu) * 6u + faceID];
    const bool varied = ((texWord >> 31) & 1u) != 0u;   // flags bit 15
    vec2 uv;
    if (varied) {
        uv = restFaceUV(P.areaAbs.xyz + restM, axis);   // read by voxel.frag's varied path (flags bit 16)
    } else if (faceID == 0u || faceID == 1u) uv = vec2(restM.x, -restM.y);
    else if (faceID == 2u)                   uv = vec2(-restM.z, -restM.y);
    else if (faceID == 3u)                   uv = vec2(restM.z, -restM.y);
    else if (faceID == 4u)                   uv = vec2(-restM.x, -restM.z);
    else                                     uv = vec2(restM.x, -restM.z);

    const float kShadowNormalOffset = 0.15;
    shadowCoord = ubo.biasedLightSpace * vec4(worldPos + outNormal * kShadowNormalOffset, 1.0);
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
    outWorldPos = worldPos;
    // voxel.frag's absolute position (cracks, debug views): the body's CURRENT place; the varied tile does not use it
    vChunkBaseAbs = P.areaAbs.xyz;
    vChunkBaseRel = P.t.xyz - P.uvOrigin.xyz * vs;
    vTint = vec3(1.0);
    vState = 0u;
    textureIndex = texWord & 0xFFFFu;
    flags = ((texWord >> 16) & 0xFFFDu) | (1u << 16);   // D4 follow-up: never the transparent bit (no OIT pass for bodies)   // bit 16: a body face (LOD bit 23 stays clear)
    texCoord = uv;
    vSkyLight = 1.0;
}
