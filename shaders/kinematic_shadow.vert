#version 450
//
// kinematic_shadow.vert — Depth-only pass for kinematic voxel objects.
//
// Minimal version of kinematic_voxel.vert — uses the same KinematicFaceData
// per-instance layout but only computes gl_Position for the shadow map.
// Push constant contains the object's model matrix plus the light space matrix.
//

layout(location = 0) in vec3 inLocalPosition;
layout(location = 1) in vec3 inScale;
layout(location = 2) in vec2 inUvOffset;      // unused
layout(location = 3) in uint inTextureIndex;  // unused
layout(location = 4) in uint inFaceId;

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
    mat4 lightSpaceMatrix;
} pc;

void main() {
    // Same corner remap and face offset as kinematic_voxel.vert
    const uint cornerRemap[6] = uint[6](0u, 2u, 1u, 1u, 2u, 3u);
    uint cornerID = cornerRemap[uint(gl_VertexIndex)];

    vec3 faceOffset = vec3(0.0);
    if      (inFaceId == 0u) faceOffset = vec3(float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 1.0);
    else if (inFaceId == 1u) faceOffset = vec3(1.0 - float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 0.0);
    else if (inFaceId == 2u) faceOffset = vec3(1.0, float((cornerID >> 1) & 1u), 1.0 - float((cornerID >> 0) & 1u));
    else if (inFaceId == 3u) faceOffset = vec3(0.0, float((cornerID >> 1) & 1u), float((cornerID >> 0) & 1u));
    else if (inFaceId == 4u) faceOffset = vec3(float((cornerID >> 0) & 1u), 1.0, 1.0 - float((cornerID >> 1) & 1u));
    else                     faceOffset = vec3(float((cornerID >> 0) & 1u), 0.0, float((cornerID >> 1) & 1u));

    vec3 localPos = inLocalPosition + (faceOffset - 0.5) * inScale;
    vec4 worldPos = pc.modelMatrix * vec4(localPos, 1.0);
    gl_Position = pc.lightSpaceMatrix * worldPos;
}
