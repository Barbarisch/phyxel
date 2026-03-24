#version 450

layout (location = 0) in vec2 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec4 inColor;

layout (push_constant) uniform PushConstants {
    vec2 scale;     // 2.0/screenWidth, 2.0/screenHeight
    vec2 translate; // -1.0, -1.0
} pc;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec4 outColor;

void main() {
    outUV = inUV;
    outColor = inColor;
    gl_Position = vec4(inPos * pc.scale + pc.translate, 0.0, 1.0);
}
