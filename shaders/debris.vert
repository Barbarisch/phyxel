#version 450

layout(push_constant) uniform PushConstants {
    mat4 view;
    mat4 proj;
} pushConsts;

// Vertex Attributes (from Vertex Buffer)
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

// Instance Attributes (from Instance Buffer)
layout(location = 2) in vec3 inInstancePos;
layout(location = 3) in vec3 inInstanceScale;
layout(location = 4) in vec4 inInstanceColor;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;

void main() {
    fragColor = inInstanceColor.rgb;
    fragNormal = inNormal;
    
    // Scale and translate
    vec3 worldPos = (inPos * inInstanceScale) + inInstancePos;
    
    gl_Position = pushConsts.proj * pushConsts.view * vec4(worldPos, 1.0);
}
