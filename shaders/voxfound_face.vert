#version 450
//
// voxfound_face.vert — docs/VoxelFoundationPlan.md F2.2: draws the 1/8 m faces built by the GPU face builder
// (FaceBuilderGpu / voxfound_faces.comp) through TODAY's voxel.frag — the same lighting, shadows, materials
// and tonemap as static_voxel.vert, so a side-by-side shows only the grid change.
//
// Instance (binding 1, 12 bytes): the 8-byte PackedFace (Mesher.h)
//   a: x 0-7 | y 8-15 | z 16-23 | axis 24-25 | dir>0 bit 26     (voxels, relative to the chunk origin)
//   b: du-1 0-7 | dv-1 8-15 | material 16-23
// plus a texture word (bits 0-15 texture index, 16-31 flags) the host looks up per material and face.
// PREVIEW STOPGAP: the texture word makes the draw instance 12 B; the 8 B record path reads the texture
// from a material table on the device instead (§4.6, owed).
//
// Geometry: the same unit-quad corner table and faceID order as static_voxel.vert (0 +Z, 1 -Z, 2 +X,
// 3 -X, 4 +Y, 5 -Y), drawn with the same 36-index cube and FRONT culling, so winding matches.
// Texture: §4.3 — UVs are a pure function of the position in METRES, oriented per face exactly as a
// whole cube face is today, so stone at 1/8 m shows today's 1 m stone tile (voxel.frag wraps with fract).

layout(location = 0) in uint vertexID;
layout(location = 1) in uint inFaceA;
layout(location = 2) in uint inFaceB;
layout(location = 3) in uint inTexWord;

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
    vec3 chunkBaseOffset;  // CAMERA-RELATIVE origin of voxel (0,0,0), metres
    uint debugMode;
    vec3 chunkBaseAbs;     // absolute origin, metres (varied-hash seed)
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

    // The quad spans du along u = (axis+1)%3 and dv along v = (axis+2)%3 (Mesher.h).
    vec3 sizeVec = vec3(1.0);
    sizeVec[(axis + 1u) % 3u] = du;
    sizeVec[(axis + 2u) % 3u] = dv;
    const vec3 localM = (o + faceOffset * sizeVec) * kVoxel;   // metres from the origin, exact (k/8)
    const vec3 worldPos = pushConstants.chunkBaseOffset + localM;

    if (faceID == 0u)      outNormal = vec3(0.0, 0.0, 1.0);
    else if (faceID == 1u) outNormal = vec3(0.0, 0.0, -1.0);
    else if (faceID == 2u) outNormal = vec3(1.0, 0.0, 0.0);
    else if (faceID == 3u) outNormal = vec3(-1.0, 0.0, 0.0);
    else if (faceID == 4u) outNormal = vec3(0.0, 1.0, 0.0);
    else                   outNormal = vec3(0.0, -1.0, 0.0);

    // Today's cube-face UV orientation, written in metres (static_voxel.vert baseUV, whole cubes):
    //   +Z (x, -y)  -Z (x, -y)  +X (-z, -y)  -X (z, -y)  +Y (-x, -z)  -Y (x, -z)
    // The origin is a whole number of metres, so fract() in voxel.frag lands on the same tile phase.
    vec2 uv;
    if (faceID == 0u || faceID == 1u) uv = vec2(localM.x, -localM.y);
    else if (faceID == 2u)            uv = vec2(-localM.z, -localM.y);
    else if (faceID == 3u)            uv = vec2(localM.z, -localM.y);
    else if (faceID == 4u)            uv = vec2(-localM.x, -localM.z);
    else                              uv = vec2(localM.x, -localM.z);

    const float kShadowNormalOffset = 0.15;
    shadowCoord = ubo.biasedLightSpace * vec4(worldPos + outNormal * kShadowNormalOffset, 1.0);
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
    outWorldPos = worldPos;
    vChunkBaseAbs = pushConstants.chunkBaseAbs;
    vChunkBaseRel = pushConstants.chunkBaseOffset;
    vTint = vec3(1.0);
    vState = 0u;
    textureIndex = inTexWord & 0xFFFFu;
    flags = (inTexWord >> 16) & 0xFFFFu;
    texCoord = uv;
    vSkyLight = 1.0;
}
