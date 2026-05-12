#version 450
//
// dynamic_shadow.vert — Depth-only pass for GPU-simulated particle debris.
//
// Minimal version of dynamic_voxel.vert using the same DynamicSubcubeInstanceData
// per-instance layout. Outputs only gl_Position for the shadow map depth pass.
//

layout(location = 0) in uint  vertexID;
layout(location = 1) in vec3  inWorldPosition;
layout(location = 2) in uint  inTextureIndex; // unused
layout(location = 3) in uint  inFaceID;
layout(location = 4) in vec3  inScale;
layout(location = 5) in vec4  inRotation;     // quaternion (x,y,z,w)
layout(location = 6) in ivec3 inLocalPosition; // unused

layout(push_constant) uniform PushConstants {
    mat4 lightSpaceMatrix;
} pc;

vec3 rotateByQuaternion(vec3 v, vec4 q) {
    vec3 qvec = q.xyz;
    float qw = q.w;
    vec3 cross1 = cross(qvec, v) + qw * v;
    vec3 cross2 = cross(qvec, cross1);
    return v + 2.0 * cross2;
}

void main() {
    const uint cornerRemap[6] = uint[6](0u, 2u, 1u, 1u, 2u, 3u);
    uint cornerID = cornerRemap[vertexID];

    vec3 faceOffset = vec3(0.0);
    if      (inFaceID == 0u) faceOffset = vec3(float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 1.0);
    else if (inFaceID == 1u) faceOffset = vec3(1.0 - float((cornerID >> 0) & 1u), float((cornerID >> 1) & 1u), 0.0);
    else if (inFaceID == 2u) faceOffset = vec3(1.0, float((cornerID >> 1) & 1u), 1.0 - float((cornerID >> 0) & 1u));
    else if (inFaceID == 3u) faceOffset = vec3(0.0, float((cornerID >> 1) & 1u), float((cornerID >> 0) & 1u));
    else if (inFaceID == 4u) faceOffset = vec3(float((cornerID >> 0) & 1u), 1.0, 1.0 - float((cornerID >> 1) & 1u));
    else                     faceOffset = vec3(float((cornerID >> 0) & 1u), 0.0, float((cornerID >> 1) & 1u));

    vec3 localOffset = rotateByQuaternion((faceOffset - 0.5) * inScale, inRotation);
    vec3 worldPos = inWorldPosition + localOffset + inScale * 0.5;
    gl_Position = pc.lightSpaceMatrix * vec4(worldPos, 1.0);
}
