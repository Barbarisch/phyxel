#version 450
//
// character_shadow.vert — Depth-only pass for character shadow casting.
//
// Generates the same cube geometry as character_instanced.vert but outputs
// only gl_Position for the shadow map depth pass. No outputs to fragment
// shader needed (shadow.frag is empty — depth is written by hardware).
//
// Per-instance data matches CharacterInstanceData layout.
// Push constant: mat4 model + mat4 lightSpaceMatrix
//

layout(location = 0) in vec3 inOffset;   // CharacterInstanceData.offset
layout(location = 1) in vec3 inScale;    // CharacterInstanceData.scale
layout(location = 2) in vec4 inColor;    // CharacterInstanceData.color (unused here)

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 lightSpaceMatrix;
} pc;

// Same cube positions as character.vert
const vec3 positions[36] = vec3[36](
    // Front
    vec3(-0.5, -0.5,  0.5), vec3( 0.5, -0.5,  0.5), vec3( 0.5,  0.5,  0.5),
    vec3( 0.5,  0.5,  0.5), vec3(-0.5,  0.5,  0.5), vec3(-0.5, -0.5,  0.5),
    // Back
    vec3( 0.5, -0.5, -0.5), vec3(-0.5, -0.5, -0.5), vec3(-0.5,  0.5, -0.5),
    vec3(-0.5,  0.5, -0.5), vec3( 0.5,  0.5, -0.5), vec3( 0.5, -0.5, -0.5),
    // Top
    vec3(-0.5,  0.5,  0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5,  0.5,  0.5),
    // Bottom
    vec3(-0.5, -0.5, -0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5, -0.5,  0.5),
    vec3( 0.5, -0.5,  0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5, -0.5, -0.5),
    // Right
    vec3( 0.5, -0.5,  0.5), vec3( 0.5, -0.5, -0.5), vec3( 0.5,  0.5, -0.5),
    vec3( 0.5,  0.5, -0.5), vec3( 0.5,  0.5,  0.5), vec3( 0.5, -0.5,  0.5),
    // Left
    vec3(-0.5, -0.5, -0.5), vec3(-0.5, -0.5,  0.5), vec3(-0.5,  0.5,  0.5),
    vec3(-0.5,  0.5,  0.5), vec3(-0.5,  0.5, -0.5), vec3(-0.5, -0.5, -0.5)
);

void main() {
    vec3 pos = positions[gl_VertexIndex];
    // Scale each voxel cube and offset within bone-group local space
    vec3 localPos = pos * inScale + inOffset;
    // Transform through bone model matrix then light space
    vec4 worldPos = pc.model * vec4(localPos, 1.0);
    gl_Position = pc.lightSpaceMatrix * worldPos;
}
