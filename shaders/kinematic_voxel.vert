#version 450
//
// kinematic_voxel.vert — Procedural face shader for KinematicVoxelObjects.
//
// Each INSTANCE represents one face of one voxel. The CPU uploads 6 face
// instances per voxel (all faces — back-face culling handles occlusion).
// gl_VertexIndex (0-5) selects the two triangles that form the quad.
//
// Push constant:  mat4 modelMatrix  (one per draw call = one KinematicVoxelObject)
// Instance attrs: localPosition, scale, textureIndex, faceId
// UBO:            shared set-0 UBO (view, proj, lighting — same as voxel.frag)
// Fragment:       voxel.frag (unchanged)
//
// Winding: CW triangles survive (cullMode=CULL_FRONT, frontFace=CCW)
// matching the dynamic_voxel pipeline convention.
//

// Per-instance attributes (one per face)
layout(location = 0) in vec3  inLocalPosition;  // voxel center in hinge-local space
layout(location = 1) in vec3  inScale;           // per-axis scale: (1,1,1) = full cube, (1,1,0.125) = thin
layout(location = 2) in uint  inTextureIndex;    // atlas texture index
layout(location = 3) in uint  inFaceId;          // 0=+Z  1=-Z  2=+X  3=-X  4=+Y  5=-Y

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
} pc;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4  view;
    mat4  proj;
    mat4  lightSpaceMatrix;
    vec3  sunDirection;
    vec3  sunColor;
    uint  numInstances;
    float ambientLight;
    float emissiveMultiplier;
} ubo;

// Outputs matching voxel.frag inputs exactly
layout(location = 0) out flat uint textureIndex;
layout(location = 1) out vec2      texCoord;
layout(location = 2) out vec4      shadowCoord;
layout(location = 3) out flat uint flags;
layout(location = 4) out vec3      outNormal;
layout(location = 5) out vec3      outWorldPos;

void main() {
    // Remap 6 vertex IDs to 4 quad corners.
    // CW winding from outside: triangles (0,2,1) and (1,2,3).
    const uint cornerRemap[6] = uint[6](0u, 2u, 1u, 1u, 2u, 3u);
    uint cornerID = cornerRemap[uint(gl_VertexIndex)];

    // Generate face vertex offset in [0,1]^3 space
    vec3 faceOffset = vec3(0.0);
    if      (inFaceId == 0u) faceOffset = vec3(float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 1.0);
    else if (inFaceId == 1u) faceOffset = vec3(1.0 - float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 0.0);
    else if (inFaceId == 2u) faceOffset = vec3(1.0, float((cornerID >> 1) & 1u), 1.0 - float((cornerID >> 0) & 1u));
    else if (inFaceId == 3u) faceOffset = vec3(0.0, float((cornerID >> 1) & 1u), float((cornerID >> 0) & 1u));
    else if (inFaceId == 4u) faceOffset = vec3(float((cornerID >> 0) & 1u), 1.0, 1.0 - float((cornerID >> 1) & 1u));
    else                     faceOffset = vec3(float((cornerID >> 0) & 1u), 0.0, float((cornerID >> 1) & 1u));

    // Vertex in local space: center voxel at inLocalPosition, scale to [-0.5, 0.5]*scale
    vec3 localPos = inLocalPosition + (faceOffset - 0.5) * inScale;

    // World position via pivot/object transform
    vec3 worldPos = (pc.modelMatrix * vec4(localPos, 1.0)).xyz;

    // UV coordinates (matching dynamic_voxel.vert conventions per face)
    vec2 uv = vec2(0.0);
    if      (inFaceId == 0u) uv = vec2(float((cornerID >> 0) & 1u), 1.0 - float((cornerID >> 1) & 1u));
    else if (inFaceId == 1u) uv = vec2(1.0 - float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u));
    else if (inFaceId == 2u) uv = vec2(float((cornerID >> 0) & 1u), 1.0 - float((cornerID >> 1) & 1u));
    else if (inFaceId == 3u) uv = vec2(float((cornerID >> 0) & 1u), 1.0 - float((cornerID >> 1) & 1u));
    else if (inFaceId == 4u) uv = vec2(1.0 - float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u));
    else                     uv = vec2(float((cornerID >> 0) & 1u), 1.0 - float((cornerID >> 1) & 1u));

    // Face normal in local space, rotated into world space by the model matrix
    // (mat3 upper-left is correct for uniform-scale rotations; doors only rotate)
    vec3 localNormal = vec3(0.0);
    if      (inFaceId == 0u) localNormal = vec3( 0.0,  0.0,  1.0);
    else if (inFaceId == 1u) localNormal = vec3( 0.0,  0.0, -1.0);
    else if (inFaceId == 2u) localNormal = vec3( 1.0,  0.0,  0.0);
    else if (inFaceId == 3u) localNormal = vec3(-1.0,  0.0,  0.0);
    else if (inFaceId == 4u) localNormal = vec3( 0.0,  1.0,  0.0);
    else                     localNormal = vec3( 0.0, -1.0,  0.0);

    outNormal = normalize(mat3(pc.modelMatrix) * localNormal);

    // Shadow coordinate
    const mat4 biasMat = mat4(
        0.5, 0.0, 0.0, 0.0,
        0.0, 0.5, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.5, 0.5, 0.0, 1.0
    );
    shadowCoord = biasMat * ubo.lightSpaceMatrix * vec4(worldPos, 1.0);

    flags        = 0u;
    textureIndex = inTextureIndex;
    texCoord     = uv;
    outWorldPos  = worldPos;

    gl_Position = ubo.proj * ubo.view * vec4(worldPos, 1.0);
}
