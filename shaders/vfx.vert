#version 450

// Lightweight VFX particle vertex shader (spell bursts, etc.).
// Mirrors debris.vert but carries a full RGBA color so the fragment
// shader can fade emissive particles via the alpha channel.

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pushConsts;

// Vertex Attributes (shared cube mesh)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;   // unused (emissive, no lighting)

// Instance Attributes
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in vec3 inInstanceScale;
layout(location = 4) in vec4 inInstanceColor;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = inInstanceColor;

    vec3 worldPos = (inPos * inInstanceScale) + inInstancePos;
    gl_Position = pushConsts.proj * pushConsts.view * vec4(worldPos, 1.0);
}
