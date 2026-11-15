#version 450
#extension GL_ARB_shader_draw_parameters : require
//
// voxfound_face_mdi.vert — docs/VoxelFoundationPlan.md F2.11: voxfound_face.vert for the ONE indirect multidraw
// over every face cluster (ClusterCullGpu). Identical except the chunk origin: per draw, from drawOrigin[gl_DrawIDARB]
// (whole metres, exact), made camera-relative as vec3(origin - camInt) + camFrac with the camera split into whole
// metres (camInt) and the rest (camFrac = camInt - cameraPos), so no large float is subtracted.
// Original header (voxfound_face.vert, F2.2): draws the 1/8 m faces built by the GPU face builder
// (FaceBuilderGpu / voxfound_faces.comp) through TODAY's voxel.frag — the same lighting, shadows, materials
// and tonemap as static_voxel.vert, so a side-by-side shows only the grid change.
//
// Instance (binding 1, 8 bytes): the PackedFace record itself (Mesher.h), nothing added (§4.6, F2.5)
//   a: x 0-7 | y 8-15 | z 16-23 | axis 24-25 | dir>0 bit 26     (voxels, relative to the chunk origin)
//   b: du-1 0-7 | dv-1 8-15 | material 16-23                       (material = the area's palette index)
// Texture word (bits 0-15 texture index, 16-31 flags) from the device table at set 1 (Preview.h faceTex):
// faceTex[material * 6 + faceID], 256 * 6 words, so every 8-bit material is in range.
//
// Geometry: the same unit-quad corner table and faceID order as static_voxel.vert (0 +Z, 1 -Z, 2 +X,
// 3 -X, 4 +Y, 5 -Y), drawn with the same 36-index cube and FRONT culling, so winding matches.
// Texture: §4.3 — UVs are a pure function of the position in METRES, oriented per face exactly as a
// whole cube face is today, so stone at 1/8 m shows today's 1 m stone tile (voxel.frag wraps with fract).

layout(location = 0) in uint vertexID;
layout(location = 1) in uint inFaceA;
layout(location = 2) in uint inFaceB;

layout(std430, set = 1, binding = 0) readonly buffer VoxFoundFaceTex {
    uint faceTex[];   // 256 * 6
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

layout(std430, set = 1, binding = 1) readonly buffer VoxFoundDrawOrigin {
    ivec4 drawOrigin[];   // per indirect command: its chunk's origin, whole metres
};

layout(push_constant) uniform PushConstants {
    vec3 camFrac;          // camInt - cameraPos (metres, small)
    uint lodBits;          // F2.14: LOD on bit 23 + scale code 24-31 (0 = off); the level bits come per command (w)
    vec3 camInt;           // the camera position rounded down to whole metres (exact in float below 2^24)
    float tj;              // F2.18: 0 today, < 0 exact sums, > 0 exact sums + an in-plane margin (NDC per y pixel x px)
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
    const ivec3 originM = drawOrigin[gl_DrawIDARB].xyz;
    const vec3 chunkBaseOffset = vec3(originM - ivec3(pushConstants.camInt)) + pushConstants.camFrac;
    const vec3 chunkBaseAbs = vec3(originM);
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
    vec3 localM = (o + faceOffset * sizeVec) * kVoxel;   // metres from the origin, exact (k/8)
    vec3 worldPos = chunkBaseOffset + localM;
    // F2.18 T-junctions (pushConstants.tj; 0 = the line above, today).
    // (1) Exact sums: (origin - camInt) + local is exact in float (multiples of 1/8 m, small), and camFrac is added
    //     last, so a world point shared by two chunks' faces is bit-identical on both sides of the chunk border.
    // (2) tj > 0: the quad grows in its own plane so its edges cover a neighbour's T-vertex sliver. Per corner,
    //     outward along u (and v) by t = tj / (the screen speed of that axis PERPENDICULAR to the edge it moves), in
    //     y-pixel NDC units: every edge moves outward by exactly tj on screen. Clamped to half a voxel; nothing behind
    //     the camera. The texture is a function of the (grown) position, so overlapping coplanar neighbours shade the
    //     same; only silhouettes move, by <= tj.
    if (pushConstants.tj != 0.0) {
        worldPos = (vec3(originM - ivec3(pushConstants.camInt)) + localM) + pushConstants.camFrac;
        if (pushConstants.tj > 0.0) {
            const uint ua = (axis + 1u) % 3u, va = (axis + 2u) % 3u;
            vec3 eu = vec3(0.0), ev = vec3(0.0);
            eu[ua] = faceOffset[ua] * 2.0 - 1.0;   // this corner's outward side along u, v
            ev[va] = faceOffset[va] * 2.0 - 1.0;
            const vec4 c = ubo.viewProj * vec4(worldPos, 1.0);
            if (c.w > 1e-3) {
                const vec4 cu = ubo.viewProj * vec4(eu, 0.0), cv = ubo.viewProj * vec4(ev, 0.0);
                const float aspect = abs(ubo.proj[1][1] / ubo.proj[0][0]);   // width / height: x in y-pixel units
                vec2 gu = (cu.xy * c.w - c.xy * cu.w) / (c.w * c.w);
                vec2 gv = (cv.xy * c.w - c.xy * cv.w) / (c.w * c.w);
                gu.x *= aspect;
                gv.x *= aspect;
                const float area = abs(gu.x * gv.y - gu.y * gv.x);
                const float su = area / max(length(gv), 1e-20);   // u's screen speed across the edge along v
                const float sv = area / max(length(gu), 1e-20);
                const float kMax = 0.5 * kVoxel;
                const float tu = min(pushConstants.tj / max(su, 1e-20), kMax);
                const float tv = min(pushConstants.tj / max(sv, 1e-20), kMax);
                const vec3 grow = eu * tu + ev * tv;
                localM += grow;
                worldPos += grow;
            }
        }
    }

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
    vChunkBaseAbs = chunkBaseAbs;
    vChunkBaseRel = chunkBaseOffset;
    vTint = vec3(1.0);
    vState = 0u;
    const uint texWord = faceTex[((inFaceB >> 16) & 0xFFu) * 6u + faceID];
    textureIndex = texWord & 0xFFFFu;
    // F2.14: voxel.frag picks the level per pixel when the on bit is pushed (the level bits alone do nothing)
    flags = ((texWord >> 16) & 0xFFFFu) |
            (pushConstants.lodBits != 0u ? (pushConstants.lodBits | uint(drawOrigin[gl_DrawIDARB].w)) : 0u);
    texCoord = uv;
    vSkyLight = 1.0;
}
